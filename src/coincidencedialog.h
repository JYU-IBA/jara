#ifndef JARA_COINCIDENCEDIALOG_H
#define JARA_COINCIDENCEDIALOG_H

#include <vector>

#include <QDialog>
#include <QList>

#include "coincidence.h"
#include "project.h"
#include "tofedata.h"

class CoincidenceSettingsEditor;
class HistogramWidget;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QSpinBox;
class SpectrumPlotWidget;
class QTableWidget;
class QTimer;

// Converts coincidence files (*.evnt) into ToF-E event files (*.asc) next to them, see coincidence.h. The coincidence
// parameters start from the detector settings and are stored there when the files are converted. The time
// differences of the beginning of the selected file are plotted against ToF and as a distribution so that the window
// can be chosen, also by dragging its edges in the plots. Each file is assigned to an existing sample or a new one, which the caller adds the converted files to as measurements.
class CoincidenceDialog : public QDialog {
    Q_OBJECT

public:
    struct ConvertedFile {
        QString path; // Of the *.asc file
        QString sample; // Name of an existing or a new sample
    };

    // Files go to the current sample by default, without one to a new sample named after the file
    CoincidenceDialog(Project &project, const Sample *currentSample, QWidget *parent = nullptr);
    void addFiles(const QStringList &fileNames);
    const QList<ConvertedFile> &converted() const { return m_converted; }
    DetectorSettings *detector() const; // Assigned to the new measurements, its coincidence parameters were updated
    void accept() override; // Converts the files

private:
    QString evntPath(int row) const;
    void chooseFiles();
    void removeSelectedFiles();
    void updateDetector();
    // Reads the file when needed, recalculates the plots when they do not cover the window and the margin or when
    // asked to, statistics always
    void updatePreview(bool recalculatePlots = false);
    void moveWindowEdge(int marker, double x); // Marker 0 at the low end, 1 at the high end
    void clearPreview(const QString &message);
    void updateConvertButton();

    Project &m_project;
    QString m_defaultSample;
    CoincidenceSettings m_settings; // Edited, copied to the detector when converting
    int m_previewSignals;
    QTableWidget *m_files;
    QComboBox *m_detectorCombo;
    QLabel *m_detectorNote;
    CoincidenceSettingsEditor *m_settingsEditor;
    QSpinBox *m_marginSpin;
    HistogramWidget *m_histogram;
    SpectrumPlotWidget *m_distribution; // Of the time differences, the window edges are dragged there
    QLabel *m_statistics;
    QLabel *m_cursorInfo;
    QDialogButtonBox *m_buttons;
    QTimer *m_previewTimer; // Statistics of large files take a moment, they are updated once the controls settle

    // The beginning of the file shown in the preview, and the parameters it was read with
    QString m_loadedFile;
    CoincidenceSettings m_loadedWith;
    EvntPreview m_preview;
    std::vector<ToFEEvent> m_plotEvents; // x the time difference, y the ToF channel
    CoincidenceSettings m_plotted; // What the plot was calculated with, the window including the margin
    bool m_hasPlot = false;

    QList<ConvertedFile> m_converted;
};

#endif // JARA_COINCIDENCEDIALOG_H
