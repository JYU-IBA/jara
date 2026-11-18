#include "preferencesdialog.h"
#include "defaults.h"
#include "elements.h"
#include "settingseditors.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent), m_experimental(Defaults::experimentalSettings()), m_detector(Defaults::detectorSettings()),
      m_elementColors(Defaults::customElementColors()) {
    setWindowTitle("Preferences");

    auto *tabs = new QTabWidget;
    tabs->addTab(createSettingsPage(), "Settings");
    tabs->addTab(createElementColorsPage(), "Element Colors");
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QWidget *PreferencesDialog::createSettingsPage() {
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

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(note);
    layout->addWidget(experimentalGroup);
    layout->addWidget(detectorGroup);
    layout->addStretch();
    return page;
}

QWidget *PreferencesDialog::createElementColorsPage() {
    auto *table = new QGridLayout;
    table->setSpacing(2);
    table->setRowMinimumHeight(Elements::LanthanideRow - 1, 8);
    m_elementGroup = new QButtonGroup(this);
    for(int z = 1; z <= std::min(Elements::maxZ(), 118); z++) {
        auto *button = new QToolButton;
        button->setText(Elements::symbol(z));
        button->setFixedSize(34, 28);
        m_elementGroup->addButton(button, z);
        const QPoint position = Elements::tablePosition(z);
        table->addWidget(button, position.y(), position.x());
        updateElementButton(z);
    }

    auto *note = new QLabel("New selections get the color of their element. Click an element to change its color.");
    note->setWordWrap(true);
    auto *resetButton = new QPushButton("Reset All Colors");
    auto *resetLayout = new QHBoxLayout;
    resetLayout->addStretch();
    resetLayout->addWidget(resetButton);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(note);
    layout->addLayout(table);
    layout->addLayout(resetLayout);
    layout->addStretch();

    connect(m_elementGroup, &QButtonGroup::idClicked, this, [this](int z) {
        const QColor color = QColorDialog::getColor(m_elementColors.value(z, Defaults::builtInElementColor(z)), this,
                                                    QString("Color of %1").arg(Elements::symbol(z)));
        if(!color.isValid()) {
            return;
        }
        if(color == Defaults::builtInElementColor(z)) {
            m_elementColors.remove(z);
        } else {
            m_elementColors.insert(z, color);
        }
        updateElementButton(z);
    });
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        const QList<int> changed = m_elementColors.keys();
        m_elementColors.clear();
        for(int z : changed) {
            updateElementButton(z);
        }
    });
    return page;
}

void PreferencesDialog::updateElementButton(int z) {
    QAbstractButton *button = m_elementGroup->button(z);
    const QColor color = m_elementColors.value(z, Defaults::builtInElementColor(z));
    const QColor text = color.lightnessF() > 0.6 ? Qt::black : Qt::white;
    // Changed colors are shown with a thicker border
    button->setStyleSheet(QString("QToolButton { background-color: %1; color: %2; border: %3px solid %4; border-radius: 3px; }")
                                  .arg(color.name(), text.name())
                                  .arg(m_elementColors.contains(z) ? 2 : 1)
                                  .arg(color.darker(160).name()));
    button->setToolTip(QString("Z = %1, %2%3")
                               .arg(z)
                               .arg(color.name())
                               .arg(m_elementColors.contains(z) ? QString(" (changed)") : QString()));
}

void PreferencesDialog::accept() {
    Defaults::setExperimentalSettings(m_experimental);
    Defaults::setDetectorSettings(m_detector);
    Defaults::setCustomElementColors(m_elementColors);
    QDialog::accept();
}
