#include "preferencesdialog.h"
#include "defaults.h"
#include "elements.h"
#include "settingseditors.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent), m_experimental(Defaults::experimentalSettings()), m_detector(Defaults::detectorSettings()),
      m_histogram(Defaults::histogramPreferences()), m_depthProfile(Defaults::depthProfilePreferences()), m_elementColors(Defaults::customElementColors()) {
    setWindowTitle("Preferences");

    auto *tabs = new QTabWidget;
    tabs->addTab(createSettingsPage(), "Settings");
    tabs->addTab(createHistogramPage(), "Histogram");
    tabs->addTab(createDepthProfilePage(), "Depth Profiles");
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

QWidget *PreferencesDialog::createHistogramPage() {
    auto binSpinBox = [](int value) {
        auto *spinBox = new QSpinBox;
        spinBox->setRange(1, 1024);
        spinBox->setValue(value);
        return spinBox;
    };
    auto *minX = binSpinBox(m_histogram.minBinX);
    auto *minY = binSpinBox(m_histogram.minBinY);
    auto *maxX = binSpinBox(m_histogram.maxBinX);
    auto *maxY = binSpinBox(m_histogram.maxBinY);
    auto binRow = [](QSpinBox *x, QSpinBox *y) {
        auto *row = new QHBoxLayout;
        row->addWidget(x);
        row->addWidget(new QLabel("×"));
        row->addWidget(y);
        row->addWidget(new QLabel("ch (ToF × E)"));
        row->addStretch();
        return row;
    };

    auto *schemeCombo = new QComboBox;
    for(ColorScheme scheme : ColorSchemes) {
        schemeCombo->addItem(colorSchemeName(scheme), static_cast<int>(scheme));
    }
    schemeCombo->setCurrentIndex(schemeCombo->findData(static_cast<int>(m_histogram.colorScheme)));
    auto *scaleCombo = new QComboBox;
    scaleCombo->addItem("Logarithmic", true);
    scaleCombo->addItem("Linear", false);
    scaleCombo->setCurrentIndex(m_histogram.logScale ? 0 : 1);

    auto *note = new QLabel("The bin size follows the zoom level within these limits. The color scale can be changed "
                            "in each histogram from its context menu.");
    note->setWordWrap(true);

    auto *page = new QWidget;
    auto *form = new QFormLayout;
    form->addRow("Minimum bin size:", binRow(minX, minY));
    form->addRow("Maximum bin size:", binRow(maxX, maxY));
    form->addRow("Color scheme:", schemeCombo);
    form->addRow("Default color scale:", scaleCombo);
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addStretch();

    // The maximum is kept at least the minimum by moving the other value
    connect(minX, &QSpinBox::valueChanged, this, [this, maxX](int value) {
        m_histogram.minBinX = value;
        maxX->setValue(std::max(maxX->value(), value));
    });
    connect(minY, &QSpinBox::valueChanged, this, [this, maxY](int value) {
        m_histogram.minBinY = value;
        maxY->setValue(std::max(maxY->value(), value));
    });
    connect(maxX, &QSpinBox::valueChanged, this, [this, minX](int value) {
        m_histogram.maxBinX = value;
        minX->setValue(std::min(minX->value(), value));
    });
    connect(maxY, &QSpinBox::valueChanged, this, [this, minY](int value) {
        m_histogram.maxBinY = value;
        minY->setValue(std::min(minY->value(), value));
    });
    connect(schemeCombo, &QComboBox::currentIndexChanged, this, [this, schemeCombo]() {
        m_histogram.colorScheme = static_cast<ColorScheme>(schemeCombo->currentData().toInt());
    });
    connect(scaleCombo, &QComboBox::currentIndexChanged, this, [this, scaleCombo]() {
        m_histogram.logScale = scaleCombo->currentData().toBool();
    });
    return page;
}

QWidget *PreferencesDialog::createDepthProfilePage() {
    auto *iterations = new QSpinBox;
    iterations->setRange(1, 1000);
    iterations->setValue(m_depthProfile.maxIterations);
    auto *tolerance = new QDoubleSpinBox;
    tolerance->setDecimals(4);
    tolerance->setRange(0.0001, 10);
    tolerance->setSuffix(" at.%");
    tolerance->setValue(m_depthProfile.tolerance * 100);
    auto *note = new QLabel("Depth profiles are iterated until no concentration used for stopping changes more than "
                            "the tolerance, or until the maximum number of iterations. Iterating can be continued in "
                            "each depth profile.");
    note->setWordWrap(true);

    auto *page = new QWidget;
    auto *form = new QFormLayout;
    form->addRow("Maximum iterations:", iterations);
    form->addRow("Tolerance:", tolerance);
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addStretch();
    connect(iterations, &QSpinBox::valueChanged, this, [this](int value) { m_depthProfile.maxIterations = value; });
    connect(tolerance, &QDoubleSpinBox::valueChanged, this, [this](double value) { m_depthProfile.tolerance = value / 100; });
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
    Defaults::setHistogramPreferences(m_histogram);
    Defaults::setDepthProfilePreferences(m_depthProfile);
    Defaults::setCustomElementColors(m_elementColors);
    QDialog::accept();
}
