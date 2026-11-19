#ifndef JARA_SETTINGSVIEW_H
#define JARA_SETTINGSVIEW_H

#include <QWidget>

class DetectorSettingsEditor;
class PhysicsSettingsEditor;
class Project;
class QListWidget;
struct DetectorSettings;
struct ExperimentalSettings;
struct PhysicsSettings;
struct Measurement;

// Tab in the main work area where experimental, detector or physics settings are edited, with the measurements they are
// assigned to
class SettingsView : public QWidget {
    Q_OBJECT

public:
    SettingsView(Project *project, ExperimentalSettings *settings, QWidget *parent = nullptr);
    SettingsView(Project *project, DetectorSettings *detector, QWidget *parent = nullptr);
    SettingsView(Project *project, PhysicsSettings *physics, QWidget *parent = nullptr);
    ExperimentalSettings *settings() const { return m_settings; } // nullptr for other settings
    DetectorSettings *detector() const { return m_detector; } // Same as above
    PhysicsSettings *physics() const { return m_physics; } // Same as above
    QString name() const;
    void updateAssigned(); // Call after measurements or their assignments change
    void refresh(); // Shows the values of detector or physics settings again after they were changed elsewhere

signals:
    void modified();
    void nameChanged();
    void editingFinished(); // Of detector or physics settings, a field was left after it was changed
    void measurementActivated(Measurement *measurement);
    void calibrationRequested(); // Of detector settings

private:
    void createLayout(QWidget *editor);

    Project *m_project;
    ExperimentalSettings *m_settings = nullptr;
    DetectorSettings *m_detector = nullptr;
    PhysicsSettings *m_physics = nullptr;
    QListWidget *m_assignedList = nullptr;
    DetectorSettingsEditor *m_detectorEditor = nullptr;
    PhysicsSettingsEditor *m_physicsEditor = nullptr;
};

#endif // JARA_SETTINGSVIEW_H
