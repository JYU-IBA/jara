#include "preferencesdialog.h"
#include "defaults.h"
#include "settingseditors.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent), m_experimental(Defaults::experimentalSettings()), m_detector(Defaults::detectorSettings()) {
    setWindowTitle("Preferences");

    auto *experimentalEditor = new ExperimentalSettingsEditor;
    experimentalEditor->setSettings(&m_experimental);
    auto *experimentalGroup = new QGroupBox("Default experimental settings");
    auto *experimentalLayout = new QVBoxLayout(experimentalGroup);
    experimentalLayout->addWidget(experimentalEditor);

    auto *detectorEditor = new DetectorSettingsEditor;
    detectorEditor->setDetector(&m_detector);
    auto *detectorGroup = new QGroupBox("Default detector settings");
    auto *detectorLayout = new QVBoxLayout(detectorGroup);
    detectorLayout->addWidget(detectorEditor);

    auto *note = new QLabel("New projects start with these settings, and new settings sets are created from them.");
    note->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addWidget(experimentalGroup);
    layout->addWidget(detectorGroup);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PreferencesDialog::accept() {
    Defaults::setExperimentalSettings(m_experimental);
    Defaults::setDetectorSettings(m_detector);
    QDialog::accept();
}
