#ifndef JARA_MASSSELECTIONWINDOW_H
#define JARA_MASSSELECTIONWINDOW_H

#include <memory>
#include <vector>

#include <QList>
#include <QWidget>

#include "massselection.h"
#include "project.h"
#include "tofedata.h"

class HistogramWidget;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QThread;
class QTreeWidget;
class QTreeWidgetItem;

// Window of the automatic selections of a measurement, see massselection.h. Finds the mass bands of the measurement's
// events, proposes their identities and the calibration of the energy detector, and shows the selections they would
// get in the ToF-E histogram. The user corrects identities, which are kept when the others are proposed again, and
// chooses the bands to select. The calculation runs in the background.
// Making the selections replaces the earlier automatic selections of the measurement: those of the same particle keep
// their ids, so spectra and depth profiles keep using them.
class MassSelectionWindow : public QWidget {
    Q_OBJECT

public:
    // The events belong to the measurement view, which owns this window
    MassSelectionWindow(Measurement *measurement, const std::vector<ToFEEvent> *events, QWidget *parent = nullptr);
    ~MassSelectionWindow() override;
    Measurement *measurement() const { return m_measurement; }

signals:
    void selectionsMade(); // The selections of the measurement were changed
    void calibrationApplied(DetectorSettings *detector); // Its energy detector calibration was changed

private:
    // What a calculation starts from and what it gives
    struct Analysis {
        bool findBands = true;
        bool propose = true;
        ExperimentalSettings settings;
        DetectorSettings detector;
        PhysicsSettings physics;
        SelectionOptions options;
        std::vector<MassBand> bands;
        std::vector<BandProposal> proposals; // Of each band, with the identities
        EnergyDetectorFit calibration;
        std::vector<BandSelection> selections; // Of each band, none for those that continue others
        QString errorMessage;
    };

    void start(bool findBands, bool propose); // A calculation from the current state
    void finished();
    void showAnalysis();
    void updateOverlay();
    void identify(QTreeWidgetItem *item);
    void clearIdentity(QTreeWidgetItem *item);
    void applyCalibration();
    void makeSelections();
    QList<Cut> proposedCuts(bool all) const; // Of the checked bands only, or of all with a selection
    int bandOf(const QTreeWidgetItem *item) const;

    Measurement *m_measurement;
    const std::vector<ToFEEvent> *m_events;
    Analysis m_analysis;
    std::vector<bool> m_checked; // Of each band
    QList<Cut> m_previewCuts; // Drawn in the histogram
    QThread *m_thread = nullptr;
    std::shared_ptr<Analysis> m_running;
    bool m_restart = false; // Another calculation is wanted when the running one is done
    bool m_restartFind = false;
    bool m_restartPropose = false;

    HistogramWidget *m_histogram;
    QTreeWidget *m_table;
    QDoubleSpinBox *m_overlapSpin;
    QLabel *m_statusLabel;
    QLabel *m_calibrationLabel;
    QLabel *m_cursorInfo;
    QPushButton *m_proposeButton;
    QPushButton *m_applyButton;
    QPushButton *m_makeButton;
};

#endif // JARA_MASSSELECTIONWINDOW_H
