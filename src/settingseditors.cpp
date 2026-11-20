#include "settingseditors.h"
#include "formlayout.h"
#include "anglecalibration.h"
#include "elementdialog.h"
#include "efficiencydialog.h"
#include "elements.h"
#include "foildialog.h"
#include "project.h"
#include "stopping.h"

#include <algorithm>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
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

// ADC number in list-mode files, -1 is shown as not set
QSpinBox *createAdcSpinBox() {
    auto *spinBox = new QSpinBox;
    spinBox->setRange(-1, 255);
    spinBox->setSpecialValueText("Not set");
    return spinBox;
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
    fitToPlaceholder(m_beamIonEdit);
    auto *beamIonButton = new QToolButton;
    beamIonButton->setText("...");
    beamIonButton->setToolTip("Select from the periodic table");
    auto *beamIonLayout = new QHBoxLayout;
    beamIonLayout->addWidget(m_beamIonEdit);
    beamIonLayout->addWidget(beamIonButton);
    beamIonLayout->addStretch(1);
    m_incidentAngleSpin = createSpinBox(MinIncidentAngle, MaxIncidentAngle, Units::Degree, 2, "°");
    m_incidentAngleSpin->setToolTip(FromSurface);

    auto *generalGroup = new QGroupBox("General");
    auto *general = createFormLayout(generalGroup);
    general->addRow("Name:", m_nameEdit);
    auto *beamGroup = new QGroupBox("Beam");
    auto *beam = createFormLayout(beamGroup);
    beam->addRow("Incident ion:", beamIonLayout);
    beam->addRow("Beam energy:", m_beamEnergySpin);
    beam->addRow("Incident angle:", m_incidentAngleSpin);
    alignFormLabels({general, beam});
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(generalGroup);
    layout->addWidget(beamGroup);

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

CoincidenceSettingsEditor::CoincidenceSettingsEditor(QWidget *parent) : QWidget(parent) {
    auto tickSpinBox = [](int minimum) {
        auto *spinBox = new QSpinBox;
        spinBox->setRange(minimum, 2000000000);
        spinBox->setSuffix(" ticks");
        return spinBox;
    };
    m_tofAdcSpin = createAdcSpinBox();
    m_tofAdcSpin->setToolTip("ADC of the ToF signal");
    m_energyAdcSpin = createAdcSpinBox();
    m_energyAdcSpin->setToolTip("ADC of the energy signal");
    m_triggerCombo = new QComboBox;
    m_triggerCombo->addItem("E", true);
    m_triggerCombo->addItem("ToF", false);
    m_triggerCombo->setToolTip("Time differences are measured from the signals of this ADC");
    m_lowSpin = tickSpinBox(-2000000000);
    m_highSpin = tickSpinBox(-2000000000);
    m_lowSpin->setToolTip("Timestamp of the other ADC minus the trigger's, both ends included");
    m_highSpin->setToolTip(m_lowSpin->toolTip());
    m_reorderSpin = tickSpinBox(0);
    m_reorderSpin->setToolTip("Signals are put in time order within this span. A signal arriving later than this "
                              "after newer signals in the file is dropped as out of order.");
    m_pairingCombo = new QComboBox;
    for(CoincidencePairing pairing : CoincidencePairings) {
        m_pairingCombo->addItem(coincidencePairingName(pairing), static_cast<int>(pairing));
    }
    m_pairingCombo->setToolTip("How triggers with several signals in their window, or signals in the window of several "
                               "triggers, are paired");

    m_layout = createFormLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->addRow("ToF ADC:", m_tofAdcSpin);
    m_layout->addRow("E ADC:", m_energyAdcSpin);
    m_layout->addRow("Trigger:", m_triggerCombo);
    m_layout->addRow("Window low:", m_lowSpin);
    m_layout->addRow("Window high:", m_highSpin);
    m_layout->addRow("Reorder span:", m_reorderSpin);
    m_layout->addRow("Pairing:", m_pairingCombo);

    auto edited = [this]() {
        m_edited = true;
        emit changed();
    };
    auto finished = [this]() {
        if(m_edited) {
            m_edited = false;
            emit editingFinished();
        }
    };
    connect(m_tofAdcSpin, &QSpinBox::valueChanged, this, [this, edited](int value) {
        m_settings->tofAdc = value;
        edited();
    });
    connect(m_energyAdcSpin, &QSpinBox::valueChanged, this, [this, edited](int value) {
        m_settings->energyAdc = value;
        edited();
    });
    // The window is kept low ≤ high by moving the other end
    connect(m_lowSpin, &QSpinBox::valueChanged, this, [this, edited](int value) {
        m_settings->windowLow = value;
        m_highSpin->setValue(std::max(m_highSpin->value(), value));
        edited();
    });
    connect(m_highSpin, &QSpinBox::valueChanged, this, [this, edited](int value) {
        m_settings->windowHigh = value;
        m_lowSpin->setValue(std::min(m_lowSpin->value(), value));
        edited();
    });
    connect(m_reorderSpin, &QSpinBox::valueChanged, this, [this, edited](int value) {
        m_settings->reorderSpan = value;
        edited();
    });
    connect(m_triggerCombo, &QComboBox::currentIndexChanged, this, [this, edited, finished]() {
        m_settings->triggerIsEnergy = m_triggerCombo->currentData().toBool();
        edited();
        finished();
    });
    connect(m_pairingCombo, &QComboBox::currentIndexChanged, this, [this, edited, finished]() {
        m_settings->pairing = static_cast<CoincidencePairing>(m_pairingCombo->currentData().toInt());
        edited();
        finished();
    });
    for(QSpinBox *spinBox : {m_tofAdcSpin, m_energyAdcSpin, m_lowSpin, m_highSpin, m_reorderSpin}) {
        connect(spinBox, &QSpinBox::editingFinished, this, finished);
    }
}

void CoincidenceSettingsEditor::setSettings(CoincidenceSettings *settings) {
    m_settings = settings;
    setEnabled(settings != nullptr);
    if(!settings) {
        return;
    }
    auto show = [](auto *widget, auto set) {
        const QSignalBlocker blocker(widget);
        set(widget);
    };
    show(m_tofAdcSpin, [&](QSpinBox *spinBox) { spinBox->setValue(settings->tofAdc); });
    show(m_energyAdcSpin, [&](QSpinBox *spinBox) { spinBox->setValue(settings->energyAdc); });
    show(m_lowSpin, [&](QSpinBox *spinBox) { spinBox->setValue(static_cast<int>(settings->windowLow)); });
    show(m_highSpin, [&](QSpinBox *spinBox) { spinBox->setValue(static_cast<int>(settings->windowHigh)); });
    show(m_reorderSpin, [&](QSpinBox *spinBox) { spinBox->setValue(static_cast<int>(std::min<qint64>(settings->reorderSpan, 2000000000))); });
    show(m_triggerCombo, [&](QComboBox *combo) { combo->setCurrentIndex(settings->triggerIsEnergy ? 0 : 1); });
    show(m_pairingCombo, [&](QComboBox *combo) { combo->setCurrentIndex(combo->findData(static_cast<int>(settings->pairing))); });
}

void CoincidenceSettingsEditor::setAdcsVisible(bool visible) {
    m_layout->setRowVisible(m_tofAdcSpin, visible);
    m_layout->setRowVisible(m_energyAdcSpin, visible);
}

DetectorSettingsEditor::DetectorSettingsEditor(QWidget *parent) : QWidget(parent) {
    m_nameEdit = new QLineEdit;
    m_scatteringAngleSpin = createSpinBox(MinScatteringAngle, MaxScatteringAngle, Units::Degree, 2, "°");
    m_scatteringAngleSpin->setToolTip(FromSurface);
    m_tofLengthSpin = createSpinBox(0, 100, Units::Millimetre, 1, " mm");
    m_tofSlopeSpin = createSpinBox(0, 1e-6, Units::Nanosecond, 6, " ns/ch");
    m_tofOffsetSpin = createSpinBox(-1e-3, 1e-3, Units::Nanosecond, 3, " ns");
    m_coincidenceLabel = new QLabel;
    m_coincidenceLabel->setWordWrap(true);
    auto *coincidenceButton = new QPushButton("Edit...");
    m_coincidenceRow = buttonField(m_coincidenceLabel, coincidenceButton);
    m_foilLabel = new QLabel;
    auto *foilButton = new QPushButton("Edit...");
    QWidget *foilField = buttonField(m_foilLabel, foilButton);
    m_efficiencyLabel = new QLabel;
    auto *efficiencyButton = new QPushButton("Edit...");
    QWidget *efficiencyField = buttonField(m_efficiencyLabel, efficiencyButton);
    m_calibrateButton = new QPushButton("ToF Calibration...");
    m_calibrateButton->setToolTip("Find the slope and offset from the leading edges of the ToF spectra of selections");
    m_distanceSpin = createSpinBox(0, 10, Units::Millimetre, 1, " mm");
    m_distanceSpin->setToolTip("From the sample to the window of the energy detector, for the angle calibration");
    m_windowWidthSpin = createSpinBox(0, 1, Units::Millimetre, 2, " mm");
    m_windowWidthSpin->setToolTip("Width of the detector window in the scattering plane, for the angle calibration");
    m_angleCalibrationLabel = new QLabel;
    m_angleCalibrationLabel->setWordWrap(true);
    m_angleCalibrateButton = new QPushButton("Angle Calibration...");
    m_angleCalibrateButton->setToolTip("Calibrate the timing coordinate of the events to the detection angle");
    m_angleCalibrationRow = buttonField(m_angleCalibrationLabel, m_angleCalibrateButton);

    // The detector as a whole, the time of flight, the angle and list-mode files, each in a group
    auto *generalGroup = new QGroupBox("General");
    auto *general = createFormLayout(generalGroup);
    general->addRow("Name:", m_nameEdit);
    general->addRow("Scattering/recoil angle:", m_scatteringAngleSpin);
    general->addRow("Efficiencies:", efficiencyField);
    auto *tofGroup = new QGroupBox("Time of flight");
    m_tofLayout = createFormLayout(tofGroup);
    QFormLayout *tofLayout = m_tofLayout;
    tofLayout->addRow("Length:", m_tofLengthSpin);
    tofLayout->addRow("Timing detector foil:", foilField);
    tofLayout->addRow("Calibration slope:", m_tofSlopeSpin);
    tofLayout->addRow("Calibration offset:", m_tofOffsetSpin);
    m_calibrateRow = buttonField(nullptr, m_calibrateButton);
    tofLayout->addRow(QString(), m_calibrateRow);
    tofLayout->setRowVisible(m_calibrateRow, false);
    auto *angleGroup = new QGroupBox("Detection angle");
    m_angleLayout = createFormLayout(angleGroup);
    m_angleLayout->addRow("Detector distance:", m_distanceSpin);
    m_angleLayout->addRow("Window width:", m_windowWidthSpin);
    m_angleLayout->addRow("Calibration:", m_angleCalibrationRow);
    m_angleLayout->setRowVisible(m_angleCalibrationRow, false);
    m_coincidenceGroup = new QGroupBox("List-mode files");
    auto *coincidenceForm = createFormLayout(m_coincidenceGroup);
    coincidenceForm->addRow("Coincidences:", m_coincidenceRow);
    coincidenceForm->labelForField(m_coincidenceRow)->setToolTip("Coincidence parameters for importing list-mode files");
    alignFormLabels({general, tofLayout, m_angleLayout, coincidenceForm});
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(generalGroup);
    layout->addWidget(tofGroup);
    layout->addWidget(angleGroup);
    layout->addWidget(m_coincidenceGroup);

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
    connectSpinBox(this, m_distanceSpin, m_detector, &DetectorSettings::detectorDistance);
    connectSpinBox(this, m_windowWidthSpin, m_detector, &DetectorSettings::windowWidth);
    for(QDoubleSpinBox *spinBox : {m_distanceSpin, m_windowWidthSpin}) {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, &DetectorSettingsEditor::showAngleCalibration);
    }
    connect(m_angleCalibrateButton, &QPushButton::clicked, this, &DetectorSettingsEditor::angleCalibrationRequested);

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
    for(QDoubleSpinBox *spinBox : {m_scatteringAngleSpin, m_tofLengthSpin, m_tofSlopeSpin, m_tofOffsetSpin, m_distanceSpin,
                                   m_windowWidthSpin}) {
        connect(spinBox, &QDoubleSpinBox::editingFinished, this, finished);
    }
    connect(coincidenceButton, &QPushButton::clicked, this, &DetectorSettingsEditor::editCoincidence);
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
    setValueSilently(m_distanceSpin, detector->detectorDistance);
    setValueSilently(m_windowWidthSpin, detector->windowWidth);
    showAngleCalibration();
    m_coincidenceLabel->setText(coincidenceText(detector->coincidence));
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

void DetectorSettingsEditor::editCoincidence() {
    QDialog dialog(this);
    dialog.setWindowTitle("Coincidence Parameters");
    CoincidenceSettings settings = m_detector->coincidence;
    auto *editor = new CoincidenceSettingsEditor;
    editor->setSettings(&settings);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("For converting list-mode files, see File → Import Event Data. Times in clock ticks."));
    layout->addWidget(editor);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if(dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_detector->coincidence = settings;
    m_coincidenceLabel->setText(coincidenceText(settings));
    emit changed();
    // The edit is finished when the dialog is closed
    m_edited = false;
    emit editingFinished();
}

void DetectorSettingsEditor::refresh() {
    setDetector(m_detector);
}

void DetectorSettingsEditor::setCalibrationAvailable(bool available) {
    m_tofLayout->setRowVisible(m_calibrateRow, available);
    m_angleLayout->setRowVisible(m_angleCalibrationRow, available);
}

void DetectorSettingsEditor::showAngleCalibration() {
    if(!m_detector) {
        return;
    }
    const AngleCalibration &calibration = m_detector->angleCalibration;
    QString text;
    if(!calibration.valid) {
        text = QStringLiteral("Not calibrated");
    } else {
        text = QString("Centre %1 %2 %3 ticks/ch × ToF, ± %4 ticks")
                       .arg(calibration.centre, 0, 'f', 2)
                       .arg(calibration.centreSlope < 0 ? "−" : "+")
                       .arg(std::abs(calibration.centreSlope), 0, 'g', 3)
                       .arg(calibration.halfWidth, 0, 'f', 2);
        if(calibration.reversed) {
            text += QStringLiteral(", reversed");
        }
        const double halfAngle = windowHalfAngle(*m_detector);
        text += halfAngle > 0 ? QString(", ± %1°").arg(halfAngle / Units::Degree, 0, 'f', 2)
                              : QStringLiteral(" (the detector distance and window width are needed)");
    }
    m_angleCalibrationLabel->setText(text);
}

void DetectorSettingsEditor::setCoincidenceVisible(bool visible) {
    m_coincidenceGroup->setVisible(visible);
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

    auto *generalGroup = new QGroupBox("General");
    auto *general = createFormLayout(generalGroup);
    general->addRow("Name:", m_nameEdit);
    auto *crossSectionGroup = new QGroupBox("Cross sections");
    auto *crossSections = createFormLayout(crossSectionGroup);
    crossSections->addRow("Screening:", m_screeningCombo);
    auto *stoppingGroup = new QGroupBox("Stopping of all elements");
    auto *stopping = createFormLayout(stoppingGroup);
    stopping->addRow("Electronic factor:", m_electronicSpin);
    stopping->addRow("Nuclear factor:", m_nuclearSpin);
    stopping->addRow("Stopping file:", m_stoppingFileCombo);
    alignFormLabels({general, crossSections, stopping});
    auto *pairGroup = new QGroupBox("Stopping of element pairs");
    auto *pairLayout = new QVBoxLayout(pairGroup);
    pairLayout->addWidget(m_correctionTable);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(generalGroup);
    layout->addWidget(crossSectionGroup);
    layout->addWidget(stoppingGroup);
    layout->addWidget(pairGroup);

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
        int selected = row;
        if(valid) {
            (incident ? correction.z1 : correction.z2) = z;
            // Sorted by the elements, the edited pair is followed
            const StoppingCorrection edited = correction;
            m_physics->sortCorrections();
            selected = static_cast<int>(m_physics->corrections.indexOf(edited));
        }
        showCorrections(); // The symbol, and the files with the pair
        m_correctionTable->selectRow(selected);
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
