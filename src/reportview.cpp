#include "reportview.h"
#include "formlayout.h"
#include "project.h"
#include "reportpdf.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int ObjectRole = Qt::UserRole; // The depth profile or split analysis of a check box
constexpr int SplitRole = Qt::UserRole + 1; // True for split analyses

enum ViewColumn { ProfileColumn, DepthMinColumn, DepthMaxColumn, ConcentrationMinColumn, ConcentrationMaxColumn, LogColumn,
                  ViewColumnCount };

QDoubleSpinBox *rangeSpinBox(double minimum, double maximum, int decimals) {
    auto *spinBox = new QDoubleSpinBox;
    spinBox->setRange(minimum, maximum);
    spinBox->setDecimals(decimals);
    spinBox->setKeyboardTracking(false);
    return spinBox;
}

QString number(double value) {
    return QString::number(value, 'g', 6);
}
}

ReportView::ReportView(Project *project, Report *report, QWidget *parent)
    : QWidget(parent), m_project(project), m_report(report) {
    m_contents = new QTreeWidget;
    m_contents->setHeaderLabel("Included");
    m_contents->setToolTip("The depth profiles in the table and the plots, the split analyses in the appendix");
    connect(m_contents, &QTreeWidget::itemChanged, this, &ReportView::contentItemChanged);

    auto *splitter = new QSplitter;
    splitter->addWidget(m_contents);
    splitter->addWidget(createOptions());
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({350, 650});

    auto *generate = new QPushButton("Generate PDF...");
    connect(generate, &QPushButton::clicked, this, &ReportView::generatePdf);
    m_statusLabel = new QLabel;
    auto *topRow = new QHBoxLayout;
    topRow->addWidget(generate);
    topRow->addWidget(m_statusLabel, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(createStaleBar());
    layout->addWidget(splitter, 1);
    refresh();
}

QString ReportView::title() const {
    return m_stale ? m_report->name + QStringLiteral(" •") : m_report->name;
}

QWidget *ReportView::createStaleBar() {
    m_staleBar = new QWidget;
    m_staleBar->setAutoFillBackground(true);
    QPalette palette = m_staleBar->palette();
    palette.setColor(QPalette::Window, QColor(0xff, 0xe0, 0x82));
    palette.setColor(QPalette::WindowText, Qt::black);
    m_staleBar->setPalette(palette);
    auto *label = new QLabel("Out of date.");
    label->setToolTip("The included depth profiles, their measurements or the options of the report have changed since "
                      "the PDF was generated.");
    auto *generate = new QPushButton("Generate PDF...");
    auto *layout = new QHBoxLayout(m_staleBar);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(label, 1);
    layout->addWidget(generate);
    connect(generate, &QPushButton::clicked, this, &ReportView::generatePdf);
    m_staleBar->hide();
    return m_staleBar;
}

QWidget *ReportView::createOptions() {
    m_titleEdit = new QLineEdit(m_report->title);
    connect(m_titleEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_report->title = text;
        changed();
    });
    m_namesCombo = new QComboBox;
    m_namesCombo->addItem("Automatic", static_cast<int>(Report::NameMode::Auto));
    m_namesCombo->addItem("Always", static_cast<int>(Report::NameMode::On));
    m_namesCombo->addItem("Never", static_cast<int>(Report::NameMode::Off));
    m_namesCombo->setCurrentIndex(m_namesCombo->findData(static_cast<int>(m_report->measurementNames)));
    m_namesCombo->setToolTip("Automatic: when a sample has several rows");
    connect(m_namesCombo, &QComboBox::currentIndexChanged, this, [this]() {
        m_report->measurementNames = static_cast<Report::NameMode>(m_namesCombo->currentData().toInt());
        changed();
    });
    auto *general = createFormLayout();
    general->addRow("Title:", m_titleEdit);
    general->addRow("Measurement names:", m_namesCombo);

    auto *settingsGroup = new QGroupBox("Settings");
    auto *settingsLayout = new QGridLayout(settingsGroup);
    const std::pair<const char *, bool ReportSettings::*> items[] = {
            {"Incident ion", &ReportSettings::ion},
            {"Beam energy", &ReportSettings::energy},
            {"Incident angle", &ReportSettings::incidentAngle},
            {"Scattering angle", &ReportSettings::scatteringAngle},
            {"ToF length", &ReportSettings::tofLength},
            {"Timing foil", &ReportSettings::foil},
            {"ToF calibration", &ReportSettings::tofCalibration},
            {"Angle calibration", &ReportSettings::angleCalibration},
            {"Efficiencies", &ReportSettings::efficiencies},
            {"Physics", &ReportSettings::physics},
            {"Depth profile parameters", &ReportSettings::profileParameters}};
    int index = 0;
    for(const auto &[text, member] : items) {
        auto *check = new QCheckBox(text);
        check->setChecked(m_report->settings.*member);
        settingsLayout->addWidget(check, index / 2, index % 2);
        index++;
        connect(check, &QCheckBox::toggled, this, [this, member](bool checked) {
            m_report->settings.*member = checked;
            changed();
        });
    }

    // Plot ranges
    m_rangeCombo = new QComboBox;
    m_rangeCombo->addItem("As in the depth profile tabs", static_cast<int>(Report::RangeMode::Gui));
    m_rangeCombo->addItem("The same for all plots", static_cast<int>(Report::RangeMode::Common));
    m_rangeCombo->addItem("Each its own", static_cast<int>(Report::RangeMode::PerProfile));
    m_rangeCombo->setCurrentIndex(m_rangeCombo->findData(static_cast<int>(m_report->rangeMode)));
    connect(m_rangeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        m_report->rangeMode = static_cast<Report::RangeMode>(m_rangeCombo->currentData().toInt());
        updateRangeControls();
        changed();
    });
    const PlotView &common = m_report->commonView;
    m_depthMinSpin = rangeSpinBox(-1e6, 1e6, 1);
    m_depthMaxSpin = rangeSpinBox(-1e6, 1e6, 1);
    m_concentrationMinSpin = rangeSpinBox(0, 100, 4);
    m_concentrationMaxSpin = rangeSpinBox(0, 100, 4);
    m_depthMinSpin->setValue(common.depthMin / Units::ThinFilmUnit);
    m_depthMaxSpin->setValue(common.depthMax / Units::ThinFilmUnit);
    m_concentrationMinSpin->setValue(common.concentrationMin * 100);
    m_concentrationMaxSpin->setValue(common.concentrationMax * 100);
    m_logCheck = new QCheckBox("Log scale");
    m_logCheck->setChecked(common.logScale);
    auto storeCommon = [this]() {
        PlotView &view = m_report->commonView;
        view.depthMin = m_depthMinSpin->value() * Units::ThinFilmUnit;
        view.depthMax = std::max(m_depthMaxSpin->value() * Units::ThinFilmUnit, view.depthMin + Units::ThinFilmUnit);
        view.concentrationMin = m_concentrationMinSpin->value() / 100;
        view.concentrationMax = std::max(m_concentrationMaxSpin->value() / 100, view.concentrationMin + 1e-6);
        view.logScale = m_logCheck->isChecked();
        changed();
    };
    for(QDoubleSpinBox *spin : {m_depthMinSpin, m_depthMaxSpin, m_concentrationMinSpin, m_concentrationMaxSpin}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, storeCommon);
    }
    connect(m_logCheck, &QCheckBox::toggled, this, storeCommon);
    m_commonRange = new QWidget;
    auto *commonLayout = new QGridLayout(m_commonRange);
    commonLayout->setContentsMargins(0, 0, 0, 0);
    commonLayout->addWidget(new QLabel("Depth (10¹⁵ at./cm²):"), 0, 0);
    commonLayout->addWidget(m_depthMinSpin, 0, 1);
    commonLayout->addWidget(new QLabel("–"), 0, 2);
    commonLayout->addWidget(m_depthMaxSpin, 0, 3);
    commonLayout->addWidget(new QLabel("Concentration (at.%):"), 1, 0);
    commonLayout->addWidget(m_concentrationMinSpin, 1, 1);
    commonLayout->addWidget(new QLabel("–"), 1, 2);
    commonLayout->addWidget(m_concentrationMaxSpin, 1, 3);
    commonLayout->addWidget(m_logCheck, 2, 1);

    m_viewTable = new QTableWidget(0, ViewColumnCount);
    m_viewTable->setHorizontalHeaderLabels({"Depth profile", "Depth min", "Depth max", "at.% min", "at.% max", "Log"});
    m_viewTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_viewTable->verticalHeader()->hide();
    m_viewTable->setToolTip("Depths in 10¹⁵ at./cm², starting from the zoom of the depth profile tabs");
    connect(m_viewTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        const auto *profile = reinterpret_cast<const DepthProfile *>(
                m_viewTable->item(item->row(), ProfileColumn)->data(ObjectRole).value<quintptr>());
        PlotView view = m_report->profileViews.value(profile, profile->view.value_or(m_report->commonView));
        const double value = item->text().toDouble();
        switch(item->column()) {
            case DepthMinColumn:
                view.depthMin = value * Units::ThinFilmUnit;
                break;
            case DepthMaxColumn:
                view.depthMax = value * Units::ThinFilmUnit;
                break;
            case ConcentrationMinColumn:
                view.concentrationMin = value / 100;
                break;
            case ConcentrationMaxColumn:
                view.concentrationMax = value / 100;
                break;
            case LogColumn:
                view.logScale = item->checkState() == Qt::Checked;
                break;
            default:
                return;
        }
        if(view.depthMax > view.depthMin && view.concentrationMax > view.concentrationMin) {
            m_report->profileViews.insert(profile, view);
        }
        changed();
    });

    auto *rangeGroup = new QGroupBox("Plot ranges");
    auto *rangeLayout = new QVBoxLayout(rangeGroup);
    rangeLayout->addWidget(m_rangeCombo);
    rangeLayout->addWidget(m_commonRange);
    rangeLayout->addWidget(m_viewTable, 1);

    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->addLayout(general);
    layout->addWidget(settingsGroup);
    layout->addWidget(rangeGroup, 1);
    auto *scroll = new QScrollArea;
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

void ReportView::changed() {
    // The project being modified refreshes this view, which is not done in the handlers of its widgets
    QTimer::singleShot(0, this, [this]() {
        emit modified();
        updateStale();
    });
}

void ReportView::refresh() {
    updateContents();
    updateRangeControls();
    updateStale();
}

void ReportView::updateContents() {
    QStringList key;
    for(size_t s = 0; s < m_project->sampleCount(); s++) {
        const Sample *sample = m_project->sample(s);
        for(const auto &measurement : sample->measurements) {
            key.append(sample->name + QLatin1Char('/') + measurement->name);
            for(const auto &profile : measurement->depthProfiles) {
                key.append(QString("p %1 %2").arg(reinterpret_cast<quintptr>(profile.get())).arg(profile->name));
            }
            for(const auto &split : measurement->splitAnalyses) {
                key.append(QString("s %1 %2").arg(reinterpret_cast<quintptr>(split.get())).arg(split->name));
            }
        }
    }
    const QSignalBlocker blocker(m_contents);
    if(key != m_contentsKey) {
        m_contentsKey = key;
        m_contents->clear();
        for(size_t s = 0; s < m_project->sampleCount(); s++) {
            const Sample *sample = m_project->sample(s);
            auto *sampleItem = new QTreeWidgetItem(m_contents, {sample->name});
            for(const auto &measurement : sample->measurements) {
                auto *measurementItem = new QTreeWidgetItem(sampleItem, {measurement->name});
                auto addItem = [measurementItem](const QString &text, const void *object, bool split) {
                    auto *item = new QTreeWidgetItem(measurementItem, {text});
                    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                    item->setData(0, ObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(object)));
                    item->setData(0, SplitRole, split);
                };
                for(const auto &profile : measurement->depthProfiles) {
                    addItem(profile->name, profile.get(), false);
                }
                for(const auto &split : measurement->splitAnalyses) {
                    addItem(QString("%1 (appendix)").arg(split->name), split.get(), true);
                }
            }
        }
        m_contents->expandAll();
    }
    for(QTreeWidgetItemIterator it(m_contents); *it; ++it) {
        if(!(*it)->data(0, ObjectRole).isValid()) {
            continue;
        }
        const auto *object = reinterpret_cast<const void *>((*it)->data(0, ObjectRole).value<quintptr>());
        const bool included = (*it)->data(0, SplitRole).toBool()
                                      ? m_report->splits.contains(static_cast<const SplitAnalysis *>(object))
                                      : m_report->profiles.contains(static_cast<const DepthProfile *>(object));
        (*it)->setCheckState(0, included ? Qt::Checked : Qt::Unchecked);
    }
}

void ReportView::contentItemChanged(QTreeWidgetItem *item) {
    if(!item->data(0, ObjectRole).isValid()) {
        return;
    }
    const auto *object = reinterpret_cast<const void *>(item->data(0, ObjectRole).value<quintptr>());
    const bool checked = item->checkState(0) == Qt::Checked;
    if(item->data(0, SplitRole).toBool()) {
        const auto *split = static_cast<const SplitAnalysis *>(object);
        m_report->splits.removeAll(split);
        if(checked) {
            m_report->splits.append(split);
        }
    } else {
        const auto *profile = static_cast<const DepthProfile *>(object);
        m_report->profiles.removeAll(profile);
        if(checked) {
            m_report->profiles.append(profile);
        }
        QTimer::singleShot(0, this, &ReportView::updateViewTable);
    }
    changed();
}

void ReportView::updateRangeControls() {
    m_commonRange->setVisible(m_report->rangeMode == Report::RangeMode::Common);
    m_viewTable->setVisible(m_report->rangeMode == Report::RangeMode::PerProfile);
    updateViewTable();
}

void ReportView::updateViewTable() {
    if(m_report->rangeMode != Report::RangeMode::PerProfile) {
        return;
    }
    // The included profiles in the order of the project
    QList<std::pair<QString, const DepthProfile *>> profiles;
    QStringList key;
    for(size_t s = 0; s < m_project->sampleCount(); s++) {
        const Sample *sample = m_project->sample(s);
        for(const auto &measurement : sample->measurements) {
            for(const auto &profile : measurement->depthProfiles) {
                if(m_report->profiles.contains(profile.get())) {
                    profiles.append({QString("%1 / %2 / %3").arg(sample->name, measurement->name, profile->name), profile.get()});
                    key.append(QString("%1 %2").arg(reinterpret_cast<quintptr>(profile.get())).arg(profiles.last().first));
                }
            }
        }
    }
    if(key == m_viewTableKey) {
        return; // Not rebuilt while the user edits it
    }
    m_viewTableKey = key;
    const QSignalBlocker blocker(m_viewTable);
    m_viewTable->setRowCount(static_cast<int>(profiles.size()));
    for(int row = 0; row < profiles.size(); row++) {
        const auto &[label, profile] = profiles[row];
        const PlotView view = m_report->profileViews.value(profile, profile->view.value_or(m_report->commonView));
        auto *name = new QTableWidgetItem(label);
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        name->setData(ObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(profile)));
        m_viewTable->setItem(row, ProfileColumn, name);
        m_viewTable->setItem(row, DepthMinColumn, new QTableWidgetItem(number(view.depthMin / Units::ThinFilmUnit)));
        m_viewTable->setItem(row, DepthMaxColumn, new QTableWidgetItem(number(view.depthMax / Units::ThinFilmUnit)));
        m_viewTable->setItem(row, ConcentrationMinColumn, new QTableWidgetItem(number(view.concentrationMin * 100)));
        m_viewTable->setItem(row, ConcentrationMaxColumn, new QTableWidgetItem(number(view.concentrationMax * 100)));
        auto *log = new QTableWidgetItem;
        log->setFlags((log->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        log->setCheckState(view.logScale ? Qt::Checked : Qt::Unchecked);
        m_viewTable->setItem(row, LogColumn, log);
    }
}

void ReportView::updateStale() {
    const bool stale = m_report->generated.isValid()
                       && !isReportUpToDate(*m_report, m_project->displayName(),
                                            [this](const DepthProfile *profile) { return m_project->measurementOf(profile); },
                                            [this](const SplitAnalysis *split) { return m_project->measurementOf(split); });
    m_staleBar->setVisible(stale);
    m_statusLabel->setText(m_report->generated.isValid()
                                   ? QString("Generated %1").arg(m_report->generated.toString("yyyy-MM-dd HH:mm"))
                                   : QStringLiteral("Not generated yet"));
    if(stale != m_stale) {
        m_stale = stale;
        emit titleChanged();
    }
}

void ReportView::generatePdf() {
    if(m_report->profiles.isEmpty() && m_report->splits.isEmpty()) {
        QMessageBox::warning(this, "Generate Report", "Include some depth profiles or split analyses.");
        return;
    }
    QString fileName = QFileDialog::getSaveFileName(this, "Generate Report",
                                                    inProjectDirectory(m_report->title + QStringLiteral(".pdf")),
                                                    "PDF files (*.pdf)");
    if(fileName.isEmpty()) {
        return;
    }
    if(QFileInfo(fileName).suffix().isEmpty()) {
        fileName += QStringLiteral(".pdf");
    }
    QString errorMessage;
    if(!writeReportPdf(*m_project, *m_report, fileName, this, errorMessage)) {
        if(!errorMessage.isEmpty()) {
            QMessageBox::critical(this, "Generate Report", errorMessage);
        }
        return;
    }
    emit modified(); // The fingerprints of the report
    updateStale();
    m_statusLabel->setText(QString("Generated %1").arg(fileName));
}
