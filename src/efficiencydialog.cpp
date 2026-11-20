#include "efficiencydialog.h"
#include "elementdialog.h"
#include "elements.h"
#include "project.h"

#include <algorithm>

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextStream>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
enum Column { IsotopeColumn, PointsColumn, EnergyColumn, EfficiencyColumn };
constexpr int ZRole = Qt::UserRole;
constexpr int ARole = Qt::UserRole + 1;
}

bool readEfficiencyFile(const QString &fileName, QList<QPointF> &points, QString &errorMessage) {
    QFile file(fileName);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorMessage = file.errorString();
        return false;
    }
    points.clear();
    QTextStream in(&file);
    int lineNumber = 0;
    while(!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        lineNumber++;
        if(line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        const QStringList columns = line.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
        bool energyOk = false;
        bool efficiencyOk = false;
        const double energy = columns.size() >= 2 ? columns[0].toDouble(&energyOk) : 0;
        const double efficiency = columns.size() >= 2 ? columns[1].toDouble(&efficiencyOk) : 0;
        if(!energyOk || !efficiencyOk || energy < 0 || efficiency < 0) {
            errorMessage = QString("Line %1 does not have an energy and an efficiency: %2").arg(lineNumber).arg(line);
            return false;
        }
        points.append({energy * Units::Megaelectronvolt, efficiency});
    }
    if(points.size() < 2) {
        errorMessage = QStringLiteral("At least two points are needed.");
        return false;
    }
    std::sort(points.begin(), points.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    return true;
}

QString efficiencyText(const QList<EfficiencyCurve> &curves) {
    if(curves.isEmpty()) {
        return QStringLiteral("None");
    }
    QStringList symbols;
    for(const EfficiencyCurve &curve : curves) {
        symbols.append(Elements::displayLabel(curve.z, curve.a));
    }
    return symbols.join(QStringLiteral(", "));
}

EfficiencyDialog::EfficiencyDialog(const QList<EfficiencyCurve> &curves, QWidget *parent)
    : QDialog(parent), m_curves(curves) {
    setWindowTitle("Detection Efficiencies");
    auto *note = new QLabel("Events are weighted by 1 / efficiency at their energy as they leave the sample. Events "
                            "outside the energies of a curve, or with an efficiency below 1e-6, are left out of depth "
                            "profiles. Isotopes without a curve are detected fully. Selections of an element use the "
                            "curve of its most abundant isotope.");
    note->setWordWrap(true);
    m_table = new QTreeWidget;
    m_table->setRootIsDecorated(false);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setHeaderLabels({"Isotope", "Points", "Energy (MeV)", "Efficiency"});
    m_table->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    auto *importButton = new QPushButton("Import...");
    m_removeButton = new QPushButton("Remove");
    auto *buttons = new QVBoxLayout;
    buttons->addWidget(importButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch(1);
    auto *row = new QHBoxLayout;
    row->addWidget(m_table, 1);
    row->addLayout(buttons);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addLayout(row, 1);
    layout->addWidget(buttonBox);

    connect(importButton, &QPushButton::clicked, this, &EfficiencyDialog::importFiles);
    connect(m_removeButton, &QPushButton::clicked, this, &EfficiencyDialog::removeCurves);
    connect(m_table, &QTreeWidget::itemSelectionChanged, this, [this]() {
        m_removeButton->setEnabled(!m_table->selectedItems().isEmpty());
    });
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateTable();
    resize(520, 340);
}

void EfficiencyDialog::updateTable() {
    sortEfficiencies(m_curves);
    m_table->clear();
    for(const EfficiencyCurve &curve : m_curves) {
        double minEfficiency = curve.points.first().y();
        double maxEfficiency = minEfficiency;
        for(const QPointF &point : curve.points) {
            minEfficiency = std::min(minEfficiency, point.y());
            maxEfficiency = std::max(maxEfficiency, point.y());
        }
        auto *item = new QTreeWidgetItem(m_table);
        item->setText(IsotopeColumn, Elements::displayLabel(curve.z, curve.a));
        item->setData(IsotopeColumn, ZRole, curve.z);
        item->setData(IsotopeColumn, ARole, curve.a);
        item->setText(PointsColumn, QString::number(curve.points.size()));
        item->setText(EnergyColumn, QString("%1–%2").arg(curve.points.first().x() / Units::Megaelectronvolt, 0, 'g', 4)
                                            .arg(curve.points.last().x() / Units::Megaelectronvolt, 0, 'g', 4));
        item->setText(EfficiencyColumn, QString("%1–%2").arg(minEfficiency, 0, 'g', 3).arg(maxEfficiency, 0, 'g', 3));
    }
    m_removeButton->setEnabled(false);
}

void EfficiencyDialog::importFiles() {
    const QStringList fileNames = QFileDialog::getOpenFileNames(this, "Import Efficiency Files", projectDirectory(),
                                                                "Efficiency files (*.eff);;All files (*)");
    for(const QString &fileName : fileNames) {
        QList<QPointF> points;
        QString errorMessage;
        if(!readEfficiencyFile(fileName, points, errorMessage)) {
            QMessageBox::warning(this, "Import Efficiency File", QString("Could not read %1:\n%2").arg(fileName, errorMessage));
            continue;
        }
        // The isotope from the file name, e.g. "1H", or chosen by the user, the element preselected when the name has
        // one
        int z = 0;
        int a = 0;
        if(!Elements::parse(QFileInfo(fileName).completeBaseName(), z, a) || !Elements::hasIsotope(z, a)) {
            ElementDialog dialog(z, 0, this, true);
            dialog.setWindowTitle(QString("Isotope of %1").arg(QFileInfo(fileName).fileName()));
            if(dialog.exec() != QDialog::Accepted || dialog.z() <= 0 || dialog.a() <= 0) {
                continue;
            }
            z = dialog.z();
            a = dialog.a();
        }
        const auto existing = std::find_if(m_curves.begin(), m_curves.end(), [z, a](const EfficiencyCurve &curve) {
            return curve.z == z && curve.a == a;
        });
        if(existing != m_curves.end()) {
            existing->points = points; // The latest import of an isotope replaces the earlier one
        } else {
            m_curves.append({z, a, points});
        }
    }
    updateTable();
}

void EfficiencyDialog::removeCurves() {
    for(const QTreeWidgetItem *item : m_table->selectedItems()) {
        const int z = item->data(IsotopeColumn, ZRole).toInt();
        const int a = item->data(IsotopeColumn, ARole).toInt();
        m_curves.removeIf([z, a](const EfficiencyCurve &curve) { return curve.z == z && curve.a == a; });
    }
    updateTable();
}
