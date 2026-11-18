#include "settingsview.h"
#include "project.h"
#include "settingseditors.h"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

namespace {
constexpr int MeasurementRole = Qt::UserRole;
}

SettingsView::SettingsView(Project *project, ExperimentalSettings *settings, QWidget *parent)
    : QWidget(parent), m_project(project), m_settings(settings) {
    auto *editor = new ExperimentalSettingsEditor;
    editor->setSettings(settings);
    connect(editor, &ExperimentalSettingsEditor::changed, this, &SettingsView::modified);
    connect(editor, &ExperimentalSettingsEditor::nameChanged, this, &SettingsView::nameChanged);
    createLayout(editor);
}

SettingsView::SettingsView(Project *project, DetectorSettings *detector, QWidget *parent)
    : QWidget(parent), m_project(project), m_detector(detector) {
    auto *editor = new DetectorSettingsEditor;
    editor->setDetector(detector);
    editor->setCalibrationAvailable(true);
    m_detectorEditor = editor;
    connect(editor, &DetectorSettingsEditor::changed, this, &SettingsView::modified);
    connect(editor, &DetectorSettingsEditor::nameChanged, this, &SettingsView::nameChanged);
    connect(editor, &DetectorSettingsEditor::editingFinished, this, &SettingsView::editingFinished);
    connect(editor, &DetectorSettingsEditor::calibrationRequested, this, &SettingsView::calibrationRequested);
    createLayout(editor);
}

void SettingsView::createLayout(QWidget *editor) {
    m_assignedList = new QListWidget;
    auto *title = new QLabel(m_settings ? "Experimental settings" : "Detector settings");
    QFont font = title->font();
    font.setBold(true);
    title->setFont(font);

    // The form keeps its natural width, the list of measurements may be long
    auto *column = new QVBoxLayout;
    column->addWidget(title);
    column->addWidget(editor);
    column->addWidget(new QLabel("Assigned measurements (double-click to open):"));
    column->addWidget(m_assignedList, 1);
    auto *layout = new QHBoxLayout(this);
    layout->addLayout(column);
    layout->addStretch(1);

    connect(m_assignedList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        emit measurementActivated(reinterpret_cast<Measurement *>(item->data(MeasurementRole).value<quintptr>()));
    });
    updateAssigned();
}

void SettingsView::refresh() {
    if(m_detectorEditor) {
        m_detectorEditor->refresh();
    }
}

QString SettingsView::name() const {
    const QString &name = m_settings ? m_settings->name : m_detector->name;
    return name.isEmpty() ? QStringLiteral("(unnamed)") : name;
}

void SettingsView::updateAssigned() {
    const QList<Measurement *> measurements = m_settings ? m_project->measurementsUsing(m_settings)
                                                         : m_project->measurementsUsing(m_detector);
    m_assignedList->clear();
    for(size_t i = 0; i < m_project->sampleCount(); i++) {
        const Sample *sample = m_project->sample(i);
        const QString sampleName = sample->name.isEmpty() ? QStringLiteral("(unnamed)") : sample->name;
        for(const auto &measurement : sample->measurements) {
            if(measurements.contains(measurement.get())) {
                auto *item = new QListWidgetItem(QString("%1 / %2").arg(sampleName, measurement->name));
                item->setData(MeasurementRole, QVariant::fromValue(reinterpret_cast<quintptr>(measurement.get())));
                m_assignedList->addItem(item);
            }
        }
    }
}
