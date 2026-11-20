#include "measurementinfoview.h"
#include "formlayout.h"
#include "elements.h"
#include "project.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace {
constexpr auto FromSurface = "Measured from the sample surface";

QString displayName(const QString &name) {
    return name.isEmpty() ? QStringLiteral("(unnamed)") : name;
}
}

MeasurementInfoView::MeasurementInfoView(Project *project, Measurement *measurement, QWidget *parent)
    : QWidget(parent), m_project(project), m_measurement(measurement) {
    m_nameEdit = new QLineEdit(measurement->name);
    m_descriptionEdit = new QLineEdit(measurement->description);
    // The file name apart from its folder, so that a long path cannot hide it
    m_fileLabel = new QLabel;
    m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_folderLabel = new QLabel;
    m_folderLabel->setWordWrap(true);
    m_folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_settingsCombo = new QComboBox;
    m_detectorCombo = new QComboBox;
    m_physicsCombo = new QComboBox;
    m_physicsCombo->setToolTip("Without physics settings, the default ones are used");
    m_beamIonLabel = new QLabel;
    m_beamEnergyLabel = new QLabel;
    m_incidentAngleLabel = new QLabel;
    m_incidentAngleLabel->setToolTip(FromSurface);
    m_scatteringAngleLabel = new QLabel;
    m_scatteringAngleLabel->setToolTip(FromSurface);
    m_exitAngleLabel = new QLabel;
    m_exitAngleLabel->setToolTip(QString("%1. Scattering angle minus incident angle (IBM geometry).").arg(FromSurface));

    auto *form = createFormLayout();
    form->addRow("Name:", m_nameEdit);
    form->addRow("Description:", m_descriptionEdit);
    form->addRow("Data file:", m_fileLabel);
    form->addRow("Folder:", m_folderLabel);
    form->addRow("Experimental settings:", m_settingsCombo);
    form->addRow("Detector settings:", m_detectorCombo);
    form->addRow("Physics settings:", m_physicsCombo);
    form->addRow("Incident ion:", m_beamIonLabel);
    form->addRow("Beam energy:", m_beamEnergyLabel);
    form->addRow("Incident angle:", m_incidentAngleLabel);
    form->addRow("Scattering angle:", m_scatteringAngleLabel);
    form->addRow("Exit angle:", m_exitAngleLabel);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addStretch();

    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_measurement->name = text;
        emit renamed(m_measurement);
        emit modified();
    });
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
        if(m_nameEdit->isModified()) {
            m_nameEdit->setModified(false);
            emit renameFinished(m_measurement);
        }
    });
    connect(m_descriptionEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_measurement->description = text;
        emit modified();
    });
    connect(m_settingsCombo, &QComboBox::activated, this, [this]() {
        const int index = m_settingsCombo->currentData().toInt();
        m_measurement->settings = index >= 0 ? m_project->settings(static_cast<size_t>(index)) : nullptr;
        showGeometry();
        emit modified();
    });
    connect(m_detectorCombo, &QComboBox::activated, this, [this]() {
        const int index = m_detectorCombo->currentData().toInt();
        m_measurement->detector = index >= 0 ? m_project->detector(static_cast<size_t>(index)) : nullptr;
        showGeometry();
        emit modified();
    });
    connect(m_physicsCombo, &QComboBox::activated, this, [this]() {
        const int index = m_physicsCombo->currentData().toInt();
        m_measurement->physics = index >= 0 ? m_project->physics(static_cast<size_t>(index)) : nullptr;
        emit modified();
    });
    refresh();
}

void MeasurementInfoView::refresh() {
    // Not while the user types
    if(!m_nameEdit->hasFocus()) {
        m_nameEdit->setText(m_measurement->name);
    }
    if(!m_descriptionEdit->hasFocus()) {
        m_descriptionEdit->setText(m_measurement->description);
    }
    const QFileInfo file(m_measurement->path);
    m_fileLabel->setText(file.fileName());
    m_folderLabel->setText(QDir::toNativeSeparators(file.absolutePath()));
    updateSettingsCombos();
    showGeometry();
}

void MeasurementInfoView::updateSettingsCombos() {
    m_settingsCombo->clear();
    m_settingsCombo->addItem("None", -1);
    for(size_t i = 0; i < m_project->settingsCount(); i++) {
        m_settingsCombo->addItem(displayName(m_project->settings(i)->name), static_cast<int>(i));
    }
    m_settingsCombo->setCurrentIndex(m_settingsCombo->findData(m_project->settingsIndex(m_measurement->settings)));
    m_detectorCombo->clear();
    m_detectorCombo->addItem("None", -1);
    for(size_t i = 0; i < m_project->detectorCount(); i++) {
        m_detectorCombo->addItem(displayName(m_project->detector(i)->name), static_cast<int>(i));
    }
    m_detectorCombo->setCurrentIndex(m_detectorCombo->findData(m_project->detectorIndex(m_measurement->detector)));
    m_physicsCombo->clear();
    m_physicsCombo->addItem("Default", -1);
    for(size_t i = 0; i < m_project->physicsCount(); i++) {
        m_physicsCombo->addItem(displayName(m_project->physics(i)->name), static_cast<int>(i));
    }
    m_physicsCombo->setCurrentIndex(m_physicsCombo->findData(m_project->physicsIndex(m_measurement->physics)));
}

void MeasurementInfoView::showGeometry() {
    // Same number formatting as in the settings editors, with the locale's decimal separator
    auto angleText = [this](double angle) { return locale().toString(angle / Units::Degree, 'f', 2) + "°"; };
    const QString noExperimental = QStringLiteral("No experimental settings");
    const QString noDetector = QStringLiteral("No detector settings");
    if(const ExperimentalSettings *settings = m_measurement->settings) {
        m_beamIonLabel->setText(settings->beamZ > 0 ? Elements::displayLabel(settings->beamZ, settings->beamA) : QStringLiteral("Not set"));
        m_beamEnergyLabel->setText(locale().toString(settings->beamEnergy / Units::Megaelectronvolt, 'f', 3) + " MeV");
        m_incidentAngleLabel->setText(angleText(settings->incidentAngle));
    } else {
        m_beamIonLabel->setText(noExperimental);
        m_beamEnergyLabel->setText(noExperimental);
        m_incidentAngleLabel->setText(noExperimental);
    }
    m_scatteringAngleLabel->setText(m_measurement->detector ? angleText(m_measurement->detector->scatteringAngle) : noDetector);
    if(!m_measurement->hasExitAngle()) {
        m_exitAngleLabel->setText("Needs experimental and detector settings");
        m_exitAngleLabel->setStyleSheet({});
        return;
    }
    const double angle = m_measurement->exitAngle();
    m_exitAngleLabel->setText(angle > 0 ? angleText(angle) : angleText(angle) + " (incident angle exceeds scattering angle)");
    m_exitAngleLabel->setStyleSheet(angle > 0 ? QString() : QStringLiteral("color: red"));
}
