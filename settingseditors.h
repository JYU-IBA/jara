#ifndef JARA_SETTINGSEDITORS_H
#define JARA_SETTINGSEDITORS_H

#include <QWidget>

struct DetectorSettings;
struct ExperimentalSettings;
class QDoubleSpinBox;
class QLineEdit;

// Forms for editing experimental and detector settings, used both in the project tree and for the program defaults.
// The settings are edited directly, changed() is emitted after each edit.

class ExperimentalSettingsEditor : public QWidget {
    Q_OBJECT

public:
    explicit ExperimentalSettingsEditor(QWidget *parent = nullptr);
    void setSettings(ExperimentalSettings *settings); // Not owned

signals:
    void changed();
    void nameChanged(const QString &name);

private slots:
    void beamIonEdited(const QString &text);
    void selectBeamIon();

private:
    void showBeamIon();

    ExperimentalSettings *m_settings = nullptr;
    QLineEdit *m_nameEdit;
    QDoubleSpinBox *m_beamEnergySpin;
    QLineEdit *m_beamIonEdit;
    QDoubleSpinBox *m_incidentAngleSpin;
};

class DetectorSettingsEditor : public QWidget {
    Q_OBJECT

public:
    explicit DetectorSettingsEditor(QWidget *parent = nullptr);
    void setDetector(DetectorSettings *detector); // Not owned

signals:
    void changed();
    void nameChanged(const QString &name);

private:
    DetectorSettings *m_detector = nullptr;
    QLineEdit *m_nameEdit;
    QDoubleSpinBox *m_scatteringAngleSpin;
    QDoubleSpinBox *m_tofLengthSpin;
    QDoubleSpinBox *m_tofSlopeSpin;
    QDoubleSpinBox *m_tofOffsetSpin;
};

#endif // JARA_SETTINGSEDITORS_H
