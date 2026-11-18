#ifndef JARA_SETTINGSVIEW_H
#define JARA_SETTINGSVIEW_H

#include <QWidget>

class Project;
class QListWidget;
struct DetectorSettings;
struct ExperimentalSettings;
struct Measurement;

// Tab in the main work area where experimental or detector settings are edited, with the measurements they are
// assigned to
class SettingsView : public QWidget {
    Q_OBJECT

public:
    SettingsView(Project *project, ExperimentalSettings *settings, QWidget *parent = nullptr);
    SettingsView(Project *project, DetectorSettings *detector, QWidget *parent = nullptr);
    ExperimentalSettings *settings() const { return m_settings; } // nullptr for detector settings
    DetectorSettings *detector() const { return m_detector; } // nullptr for experimental settings
    QString name() const;
    void updateAssigned(); // Call after measurements or their assignments change

signals:
    void modified();
    void nameChanged();
    void measurementActivated(Measurement *measurement);

private:
    void createLayout(QWidget *editor);

    Project *m_project;
    ExperimentalSettings *m_settings = nullptr;
    DetectorSettings *m_detector = nullptr;
    QListWidget *m_assignedList = nullptr;
};

#endif // JARA_SETTINGSVIEW_H
