#include "settingseditors.h"
#include "elementdialog.h"
#include "elements.h"
#include "project.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>

namespace {
constexpr auto FromSurface = "Measured from the sample surface";

QDoubleSpinBox *createSpinBox(double minimum, double maximum, int decimals, const QString &suffix) {
    auto *spinBox = new QDoubleSpinBox;
    spinBox->setDecimals(decimals); // Before the range, which is rounded to the decimals
    spinBox->setRange(minimum, maximum);
    spinBox->setSuffix(suffix);
    return spinBox;
}

void setValueSilently(QDoubleSpinBox *spinBox, double value) {
    const QSignalBlocker blocker(spinBox);
    spinBox->setValue(value);
}

// Spin box signals are blocked while values are shown, so changes are user edits
template<typename T, typename Editor>
void connectSpinBox(Editor *editor, QDoubleSpinBox *spinBox, T *const &target, double T::*member) {
    QObject::connect(spinBox, &QDoubleSpinBox::valueChanged, editor, [editor, &target, member](double value) {
        target->*member = value;
        emit editor->changed();
    });
}
}

ExperimentalSettingsEditor::ExperimentalSettingsEditor(QWidget *parent) : QWidget(parent) {
    m_nameEdit = new QLineEdit;
    m_beamEnergySpin = createSpinBox(0, 1000, 3, " MeV");
    m_beamIonEdit = new QLineEdit;
    m_beamIonEdit->setPlaceholderText("e.g. 35Cl");
    auto *beamIonButton = new QToolButton;
    beamIonButton->setText("...");
    beamIonButton->setToolTip("Select from the periodic table");
    auto *beamIonLayout = new QHBoxLayout;
    beamIonLayout->addWidget(m_beamIonEdit);
    beamIonLayout->addWidget(beamIonButton);
    m_incidentAngleSpin = createSpinBox(MinIncidentAngle, MaxIncidentAngle, 2, "°");
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
    m_scatteringAngleSpin = createSpinBox(MinScatteringAngle, MaxScatteringAngle, 2, "°");
    m_scatteringAngleSpin->setToolTip(FromSurface);
    m_tofLengthSpin = createSpinBox(0, 100, 4, " m");
    m_tofSlopeSpin = createSpinBox(-1000, 1000, 6, " ns/ch");
    m_tofOffsetSpin = createSpinBox(-1e6, 1e6, 3, " ns");

    auto *layout = new QFormLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addRow("Name:", m_nameEdit);
    layout->addRow("Scattering/recoil angle:", m_scatteringAngleSpin);
    layout->addRow("ToF length:", m_tofLengthSpin);
    layout->addRow("ToF calibration slope:", m_tofSlopeSpin);
    layout->addRow("ToF calibration offset:", m_tofOffsetSpin);

    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_detector->name = text;
        emit nameChanged(text);
        emit changed();
    });
    connectSpinBox(this, m_scatteringAngleSpin, m_detector, &DetectorSettings::scatteringAngle);
    connectSpinBox(this, m_tofLengthSpin, m_detector, &DetectorSettings::tofLength);
    connectSpinBox(this, m_tofSlopeSpin, m_detector, &DetectorSettings::tofSlope);
    connectSpinBox(this, m_tofOffsetSpin, m_detector, &DetectorSettings::tofOffset);
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
}
