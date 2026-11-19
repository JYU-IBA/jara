#include "settingseditors.h"
#include "elementdialog.h"
#include "efficiencydialog.h"
#include "elements.h"
#include "foildialog.h"
#include "project.h"
#include "stopping.h"

#include <algorithm>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr auto FromSurface = "Measured from the sample surface";

// Values are stored in SI units, spin boxes show them in the given unit. The range is in stored units too.
const char *UnitProperty = "unit";

QDoubleSpinBox *createSpinBox(double minimum, double maximum, double unit, int decimals, const QString &suffix) {
    auto *spinBox = new QDoubleSpinBox;
    spinBox->setProperty(UnitProperty, unit);
    spinBox->setDecimals(decimals); // Before the range, which is rounded to the decimals
    spinBox->setRange(minimum / unit, maximum / unit);
    spinBox->setSuffix(suffix);
    return spinBox;
}

void setValueSilently(QDoubleSpinBox *spinBox, double value) {
    const QSignalBlocker blocker(spinBox);
    spinBox->setValue(value / spinBox->property(UnitProperty).toDouble());
}

// Spin box signals are blocked while values are shown, so changes are user edits
template<typename T, typename Editor>
void connectSpinBox(Editor *editor, QDoubleSpinBox *spinBox, T *const &target, double T::*member) {
    const double unit = spinBox->property(UnitProperty).toDouble();
    QObject::connect(spinBox, &QDoubleSpinBox::valueChanged, editor, [editor, &target, member, unit](double value) {
        target->*member = value * unit;
        emit editor->changed();
    });
}
}

ExperimentalSettingsEditor::ExperimentalSettingsEditor(QWidget *parent) : QWidget(parent) {
    m_nameEdit = new QLineEdit;
    m_beamEnergySpin = createSpinBox(0, 1000 * Units::Megaelectronvolt, Units::Megaelectronvolt, 3, " MeV");
    m_beamIonEdit = new QLineEdit;
    m_beamIonEdit->setPlaceholderText("e.g. 35Cl");
    auto *beamIonButton = new QToolButton;
    beamIonButton->setText("...");
    beamIonButton->setToolTip("Select from the periodic table");
    auto *beamIonLayout = new QHBoxLayout;
    beamIonLayout->addWidget(m_beamIonEdit);
    beamIonLayout->addWidget(beamIonButton);
    m_incidentAngleSpin = createSpinBox(MinIncidentAngle, MaxIncidentAngle, Units::Degree, 2, "°");
    m_incidentAngleSpin->setToolTip(FromSurface);

    auto *layout = new QFormLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addRow("Name:", m_nameEdit);
    layout->addRow("Beam energy:", m_beamEnergySpin);
    layout->addRow("Incident ion:", beamIonLayout);
    layout->addRow("Incident angle:", m_incidentAngleSpin);

    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_settings->name = text;
        emit nameChanged(text);
        emit changed();
    });
    connectSpinBox(this, m_beamEnergySpin, m_settings, &ExperimentalSettings::beamEnergy);
    connectSpinBox(this, m_incidentAngleSpin, m_settings, &ExperimentalSettings::incidentAngle);
    connect(m_beamIonEdit, &QLineEdit::textEdited, this, &ExperimentalSettingsEditor::beamIonEdited);
    connect(m_beamIonEdit, &QLineEdit::editingFinished, this, [this]() {
        if(m_settings) {
            showBeamIon(); // Invalid input is not kept
        }
    });
    connect(beamIonButton, &QToolButton::clicked, this, &ExperimentalSettingsEditor::selectBeamIon);
}

void ExperimentalSettingsEditor::setSettings(ExperimentalSettings *settings) {
    m_settings = settings;
    setEnabled(settings != nullptr);
    if(!settings) {
        return;
    }
    m_nameEdit->setText(settings->name);
    setValueSilently(m_beamEnergySpin, settings->beamEnergy);
    setValueSilently(m_incidentAngleSpin, settings->incidentAngle);
    showBeamIon();
}

void ExperimentalSettingsEditor::showBeamIon() {
    m_beamIonEdit->setText(m_settings->beamZ > 0 ? Elements::label(m_settings->beamZ, m_settings->beamA) : QString());
    m_beamIonEdit->setStyleSheet({});
    m_beamIonEdit->setToolTip({});
}

void ExperimentalSettingsEditor::beamIonEdited(const QString &text) {
    // The incident ion must be a specific isotope. Only valid input is stored.
    int z = 0;
    int a = 0;
    const bool valid = Elements::parse(text, z, a) && a > 0;
    m_beamIonEdit->setStyleSheet(valid ? QString() : QStringLiteral("color: red"));
    m_beamIonEdit->setToolTip(valid ? QString() : QStringLiteral("Give an isotope, e.g. 35Cl, Cl-35 or Cl35"));
    if(valid) {
        m_settings->beamZ = z;
        m_settings->beamA = a;
        emit changed();
    }
}

void ExperimentalSettingsEditor::selectBeamIon() {
    ElementDialog dialog(m_settings->beamZ, m_settings->beamA, this, true);
    if(dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_settings->beamZ = dialog.z();
    m_settings->beamA = dialog.a();
    showBeamIon();
    emit changed();
}

DetectorSettingsEditor::DetectorSettingsEditor(QWidget *parent) : QWidget(parent) {
    m_nameEdit = new QLineEdit;
    m_scatteringAngleSpin = createSpinBox(MinScatteringAngle, MaxScatteringAngle, Units::Degree, 2, "°");
    m_scatteringAngleSpin->setToolTip(FromSurface);
    m_tofLengthSpin = createSpinBox(0, 100, Units::Millimetre, 1, " mm");
    m_tofSlopeSpin = createSpinBox(0, 1e-6, Units::Nanosecond, 6, " ns/ch");
    m_tofOffsetSpin = createSpinBox(-1e-3, 1e-3, Units::Nanosecond, 3, " ns");
    m_foilLabel = new QLabel;
    auto *foilButton = new QPushButton("Edit...");
    auto *foilLayout = new QHBoxLayout;
    foilLayout->addWidget(m_foilLabel, 1);
    foilLayout->addWidget(foilButton);
    m_efficiencyLabel = new QLabel;
    auto *efficiencyButton = new QPushButton("Edit...");
    auto *efficiencyLayout = new QHBoxLayout;
    efficiencyLayout->addWidget(m_efficiencyLabel, 1);
    efficiencyLayout->addWidget(efficiencyButton);
    m_calibrateButton = new QPushButton("ToF Calibration...");
    m_calibrateButton->setToolTip("Find the slope and offset from the leading edges of the ToF spectra of selections");
    m_calibrateButton->hide();

    auto *layout = new QFormLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addRow("Name:", m_nameEdit);
    layout->addRow("Scattering/recoil angle:", m_scatteringAngleSpin);
    layout->addRow("ToF length:", m_tofLengthSpin);
    layout->addRow("Timing detector foil:", foilLayout);
    layout->addRow("Efficiencies:", efficiencyLayout);
    layout->addRow("ToF calibration slope:", m_tofSlopeSpin);
    layout->addRow("ToF calibration offset:", m_tofOffsetSpin);
    layout->addRow(QString(), m_calibrateButton);

    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_detector->name = text;
        emit nameChanged(text);
        emit changed();
    });
    connectSpinBox(this, m_scatteringAngleSpin, m_detector, &DetectorSettings::scatteringAngle);
    connectSpinBox(this, m_tofLengthSpin, m_detector, &DetectorSettings::tofLength);
    connectSpinBox(this, m_tofSlopeSpin, m_detector, &DetectorSettings::tofSlope);
    connectSpinBox(this, m_tofOffsetSpin, m_detector, &DetectorSettings::tofOffset);
    connect(m_calibrateButton, &QPushButton::clicked, this, &DetectorSettingsEditor::calibrationRequested);
    connect(foilButton, &QPushButton::clicked, this, &DetectorSettingsEditor::editFoil);
    connect(efficiencyButton, &QPushButton::clicked, this, &DetectorSettingsEditor::editEfficiencies);

    connect(this, &DetectorSettingsEditor::changed, this, [this]() { m_edited = true; });
    auto finished = [this]() {
        if(m_edited) {
            m_edited = false;
            emit editingFinished();
        }
    };
    connect(m_nameEdit, &QLineEdit::editingFinished, this, finished);
    for(QDoubleSpinBox *spinBox : {m_scatteringAngleSpin, m_tofLengthSpin, m_tofSlopeSpin, m_tofOffsetSpin}) {
        connect(spinBox, &QDoubleSpinBox::editingFinished, this, finished);
    }
}

void DetectorSettingsEditor::setDetector(DetectorSettings *detector) {
    m_detector = detector;
    setEnabled(detector != nullptr);
    if(!detector) {
        return;
    }
    m_nameEdit->setText(detector->name);
    setValueSilently(m_scatteringAngleSpin, detector->scatteringAngle);
    setValueSilently(m_tofLengthSpin, detector->tofLength);
    setValueSilently(m_tofSlopeSpin, detector->tofSlope);
    setValueSilently(m_tofOffsetSpin, detector->tofOffset);
    m_foilLabel->setText(foilText(detector->foilMaterial, detector->foilThickness));
    m_efficiencyLabel->setText(efficiencyText(detector->efficiencies));
}

void DetectorSettingsEditor::editEfficiencies() {
    EfficiencyDialog dialog(m_detector->efficiencies, this);
    if(dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_detector->efficiencies = dialog.curves();
    m_efficiencyLabel->setText(efficiencyText(m_detector->efficiencies));
    emit changed();
    m_edited = false;
    emit editingFinished();
}

void DetectorSettingsEditor::editFoil() {
    FoilDialog dialog(m_detector->foilMaterial, m_detector->foilThickness, this);
    if(dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_detector->foilMaterial = dialog.material();
    m_detector->foilThickness = dialog.thickness();
    m_foilLabel->setText(foilText(m_detector->foilMaterial, m_detector->foilThickness));
    emit changed();
    // The edit is finished when the dialog is closed
    m_edited = false;
    emit editingFinished();
}

void DetectorSettingsEditor::refresh() {
    setDetector(m_detector);
}

void DetectorSettingsEditor::setCalibrationAvailable(bool available) {
    m_calibrateButton->setVisible(available);
}

namespace {
constexpr int UnavailableRole = Qt::UserRole + 1; // Of combo box items of files that are not available

enum CorrectionColumn { IncidentColumn, TargetColumn, ElectronicColumn, NuclearColumn, FileColumn };
constexpr double MinElectronicFactor = 0.01;
constexpr double MinNuclearFactor = 0; // Nuclear stopping can be left out
constexpr double MaxFactor = 10;

// Electronic stopping files to choose from, the first item stands for the empty name
void addStoppingFiles(QComboBox *combo, const QString &emptyText, const QString &current, const QStringList &names) {
    combo->addItem(emptyText, QString());
    for(const QString &name : names) {
        combo->addItem(name, name);
    }
    if(!current.isEmpty() && combo->findData(current) < 0) {
        // From another computer or another pair, kept until something else is chosen
        combo->addItem(QString("%1 (not available)").arg(current), current);
        combo->setItemData(combo->count() - 1, true, UnavailableRole);
    }
    combo->setCurrentIndex(combo->findData(current));
}

// After another choice, the file that was not available is not offered any more
void removeUnavailable(QComboBox *combo) {
    for(int i = combo->count() - 1; i >= 0; i--) {
        if(i != combo->currentIndex() && combo->itemData(i, UnavailableRole).toBool()) {
            combo->removeItem(i);
        }
    }
}

QDoubleSpinBox *factorSpinBox(double minimum) {
    auto *spinBox = new QDoubleSpinBox;
    spinBox->setDecimals(4);
    spinBox->setRange(minimum, MaxFactor);
    spinBox->setSingleStep(0.01);
    return spinBox;
}
}

PhysicsSettingsEditor::PhysicsSettingsEditor(QWidget *parent) : QWidget(parent) {
    m_nameEdit = new QLineEdit;
    m_screeningCombo = new QComboBox;
    for(const ScreeningModel model : ScreeningModels) {
        m_screeningCombo->addItem(screeningModelName(model), static_cast<int>(model));
    }
    m_screeningCombo->setToolTip("Screening correction of the Rutherford cross sections of depth profiles");
    m_electronicSpin = factorSpinBox(MinElectronicFactor);
    m_electronicSpin->setToolTip("Electronic stopping of all elements is multiplied by this");
    m_nuclearSpin = factorSpinBox(MinNuclearFactor);
    m_nuclearSpin->setToolTip("Nuclear stopping of all elements is multiplied by this");
    m_stoppingFileCombo = new QComboBox;
    m_stoppingFileCombo->setToolTip("GSTO file of electronic stopping of all elements, or the one JIBAL chooses for each pair");

    m_correctionTable = new QTableWidget(0, 5);
    m_correctionTable->setHorizontalHeaderLabels({"Incident", "Target", "Electronic", "Nuclear", "Stopping file"});
    m_correctionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_correctionTable->horizontalHeader()->setStretchLastSection(true);
    m_correctionTable->verticalHeader()->hide();
    m_correctionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_correctionTable->setMinimumWidth(480); // All columns fit, the stopping file names too
    m_correctionTable->setToolTip("Stopping of incident ions of one element in another, or in any element without a "
                                  "pair of its own. The factors multiply those of all elements. Right-click to add or "
                                  "remove pairs.");
    m_correctionTable->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *layout = new QFormLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addRow("Name:", m_nameEdit);
    layout->addRow("Screening:", m_screeningCombo);
    layout->addRow("Electronic stopping factor:", m_electronicSpin);
    layout->addRow("Nuclear stopping factor:", m_nuclearSpin);
    layout->addRow("Stopping file:", m_stoppingFileCombo);
    layout->addRow("Element pairs:", m_correctionTable);

    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_physics->name = text;
        emit nameChanged(text);
        emit changed();
    });
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
        if(m_edited) {
            m_edited = false;
            emit editingFinished();
        }
    });
    connect(m_screeningCombo, &QComboBox::activated, this, [this]() {
        m_physics->screening = static_cast<ScreeningModel>(m_screeningCombo->currentData().toInt());
        finishEdit();
    });
    for(QDoubleSpinBox *spinBox : {m_electronicSpin, m_nuclearSpin}) {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, [this, spinBox](double value) {
            (spinBox == m_electronicSpin ? m_physics->electronicFactor : m_physics->nuclearFactor) = value;
            m_edited = true;
            emit changed();
        });
        connect(spinBox, &QDoubleSpinBox::editingFinished, this, [this]() {
            if(m_edited) {
                m_edited = false;
                emit editingFinished();
            }
        });
    }
    connect(m_stoppingFileCombo, &QComboBox::activated, this, [this]() {
        m_physics->stoppingFile = m_stoppingFileCombo->currentData().toString();
        removeUnavailable(m_stoppingFileCombo);
        finishEdit();
    });
    connect(m_correctionTable, &QTableWidget::itemChanged, this, &PhysicsSettingsEditor::correctionItemChanged);
    connect(m_correctionTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        // Right-clicking an unselected pair selects only it
        if(QTableWidgetItem *item = m_correctionTable->itemAt(pos); item && !item->isSelected()) {
            m_correctionTable->selectRow(item->row());
        }
        QMenu menu;
        menu.addAction("Add Pair", this, &PhysicsSettingsEditor::addCorrection);
        menu.addAction("Remove", this, &PhysicsSettingsEditor::removeCorrections)
                ->setEnabled(m_correctionTable->selectionModel()->hasSelection());
        menu.addAction("Remove All", this, &PhysicsSettingsEditor::removeAllCorrections)
                ->setEnabled(m_correctionTable->rowCount() > 0);
        menu.exec(m_correctionTable->viewport()->mapToGlobal(pos));
    });
}

void PhysicsSettingsEditor::setPhysics(PhysicsSettings *physics) {
    m_physics = physics;
    setEnabled(physics != nullptr);
    if(!physics) {
        return;
    }
    m_nameEdit->setText(physics->name);
    m_screeningCombo->setCurrentIndex(m_screeningCombo->findData(static_cast<int>(physics->screening)));
    {
        const QSignalBlocker electronic(m_electronicSpin);
        const QSignalBlocker nuclear(m_nuclearSpin);
        m_electronicSpin->setValue(physics->electronicFactor);
        m_nuclearSpin->setValue(physics->nuclearFactor);
    }
    m_stoppingFileCombo->clear();
    addStoppingFiles(m_stoppingFileCombo, "Automatic (JIBAL)", physics->stoppingFile, stoppingFileNames());
    showCorrections();
}

void PhysicsSettingsEditor::refresh() {
    setPhysics(m_physics);
}

void PhysicsSettingsEditor::finishEdit() {
    emit changed();
    m_edited = false;
    emit editingFinished();
}

void PhysicsSettingsEditor::showCorrections() {
    const QSignalBlocker blocker(m_correctionTable);
    m_correctionTable->setRowCount(static_cast<int>(m_physics->corrections.size()));
    for(int row = 0; row < m_physics->corrections.size(); row++) {
        const StoppingCorrection &correction = m_physics->corrections[row];
        auto element = [](int z) {
            // Edited as a symbol or an atomic number
            return new QTableWidgetItem(z == AnyElement ? QStringLiteral("Any") : z > 0 ? Elements::symbol(z) : QString());
        };
        auto factor = [](double value) {
            auto *item = new QTableWidgetItem;
            item->setData(Qt::EditRole, value);
            return item;
        };
        m_correctionTable->setItem(row, IncidentColumn, element(correction.z1));
        m_correctionTable->setItem(row, TargetColumn, element(correction.z2));
        m_correctionTable->setItem(row, ElectronicColumn, factor(correction.electronicFactor));
        m_correctionTable->setItem(row, NuclearColumn, factor(correction.nuclearFactor));
        auto *fileCombo = new QComboBox;
        // Only the files with this pair, a file chosen earlier for another pair is shown as not available
        addStoppingFiles(fileCombo, "Same as all", correction.stoppingFile,
                         stoppingFileNames(correction.z1, correction.z2));
        connect(fileCombo, &QComboBox::activated, this, [this, fileCombo, row]() {
            m_physics->corrections[row].stoppingFile = fileCombo->currentData().toString();
            removeUnavailable(fileCombo);
            finishEdit();
        });
        m_correctionTable->setCellWidget(row, FileColumn, fileCombo);
    }
}

void PhysicsSettingsEditor::correctionItemChanged(QTableWidgetItem *item) {
    if(item->column() == IncidentColumn || item->column() == TargetColumn) {
        // A symbol or an atomic number, shown as the symbol, or "Any" (-1) for the target. Invalid elements and pairs
        // already in the table are not taken.
        const int row = item->row(); // The item is replaced below
        StoppingCorrection &correction = m_physics->corrections[row];
        const QString text = item->text().trimmed();
        const bool incident = item->column() == IncidentColumn;
        int z = 0;
        int a = 0;
        if(!incident && (text.compare("any", Qt::CaseInsensitive) == 0 || text == QString::number(AnyElement))) {
            z = AnyElement;
        } else if(!Elements::parse(text, z, a) || !Elements::isKnown(z)) {
            z = 0;
        }
        // Rows with an element still missing (zero) are not checked for duplicates
        const int z1 = incident ? z : correction.z1;
        const int z2 = incident ? correction.z2 : z;
        const bool valid = z != 0 && (z1 == 0 || z2 == 0 || !m_physics->correction(z1, z2));
        if(valid) {
            (incident ? correction.z1 : correction.z2) = z;
        }
        showCorrections(); // The symbol, and the files with the pair
        m_correctionTable->selectRow(row);
        if(valid) {
            finishEdit();
        }
        return;
    }
    if(item->column() != ElectronicColumn && item->column() != NuclearColumn) {
        return;
    }
    StoppingCorrection &correction = m_physics->corrections[item->row()];
    double &factor = item->column() == ElectronicColumn ? correction.electronicFactor : correction.nuclearFactor;
    const double minimum = item->column() == ElectronicColumn ? MinElectronicFactor : MinNuclearFactor;
    factor = std::clamp(item->data(Qt::EditRole).toDouble(), minimum, MaxFactor);
    const QSignalBlocker blocker(m_correctionTable);
    item->setData(Qt::EditRole, factor); // Out of range values are not kept
    finishEdit();
}

void PhysicsSettingsEditor::addCorrection() {
    // An empty row, its elements are typed in. One is enough, a second is not added.
    if(!m_physics->correction(0, 0)) {
        m_physics->corrections.append({0, 0});
        showCorrections();
        emit changed();
    }
    const int row = static_cast<int>(m_physics->corrections.indexOf(*m_physics->correction(0, 0)));
    m_correctionTable->setCurrentCell(row, IncidentColumn);
    m_correctionTable->editItem(m_correctionTable->item(row, IncidentColumn));
}

void PhysicsSettingsEditor::removeAllCorrections() {
    m_physics->corrections.clear();
    showCorrections();
    finishEdit();
}

void PhysicsSettingsEditor::removeCorrections() {
    QList<int> rows;
    for(const QModelIndex &index : m_correctionTable->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<>());
    for(const int row : rows) {
        m_physics->corrections.removeAt(row);
    }
    showCorrections();
    finishEdit();
}
