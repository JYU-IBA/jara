#ifndef JARA_SETTINGSEDITORS_H
#define JARA_SETTINGSEDITORS_H

#include <QWidget>

struct DetectorSettings;
struct ExperimentalSettings;
struct PhysicsSettings;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

// Forms for editing experimental, detector and physics settings, used both in the project tree and for the program
// defaults.
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
    void refresh(); // Shows the values again after the detector was changed elsewhere
    void setCalibrationAvailable(bool available); // Shows the button for the ToF calibration tool, hidden by default

signals:
    void changed();
    void nameChanged(const QString &name);
    void editingFinished(); // A field was left or Enter was pressed after changed() was emitted
    void calibrationRequested();

private slots:
    void editFoil();
    void editEfficiencies();

private:
    DetectorSettings *m_detector = nullptr;
    bool m_edited = false; // Since editingFinished() was emitted
    QLineEdit *m_nameEdit;
    QDoubleSpinBox *m_scatteringAngleSpin;
    QDoubleSpinBox *m_tofLengthSpin;
    QDoubleSpinBox *m_tofSlopeSpin;
    QDoubleSpinBox *m_tofOffsetSpin;
    QLabel *m_foilLabel;
    QLabel *m_efficiencyLabel;
    QPushButton *m_calibrateButton;
};

// Stopping of all elements and corrections for pairs of incident and target elements, shown in a table
class PhysicsSettingsEditor : public QWidget {
    Q_OBJECT

public:
    explicit PhysicsSettingsEditor(QWidget *parent = nullptr);
    void setPhysics(PhysicsSettings *physics); // Not owned
    void refresh(); // Shows the values again after the physics settings were changed elsewhere

signals:
    void changed();
    void nameChanged(const QString &name);
    void editingFinished(); // A field was left or Enter was pressed after changed() was emitted

private slots:
    void addCorrection();
    void removeCorrections(); // The selected ones
    void removeAllCorrections();
    void correctionItemChanged(QTableWidgetItem *item);

private:
    void showCorrections();
    void finishEdit(); // After an edit that is finished at once, e.g. a choice from a combo box or a dialog

    PhysicsSettings *m_physics = nullptr;
    bool m_edited = false; // Since editingFinished() was emitted
    QLineEdit *m_nameEdit;
    QComboBox *m_screeningCombo;
    QDoubleSpinBox *m_electronicSpin;
    QDoubleSpinBox *m_nuclearSpin;
    QComboBox *m_stoppingFileCombo;
    QTableWidget *m_correctionTable; // Pairs are added and removed from its context menu
};

#endif // JARA_SETTINGSEDITORS_H
