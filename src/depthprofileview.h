#ifndef JARA_DEPTHPROFILEVIEW_H
#define JARA_DEPTHPROFILEVIEW_H

#include <memory>
#include <vector>

#include <QWidget>

#include "depthprofile.h"
#include "spectrumplotwidget.h"
#include "tofedata.h"

class MeasurementRangeWidget;
class QCheckBox;
class QActionGroup;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

// Tab in the main work area showing a depth profile of a measurement. Loads the measurement's data file when created
// and iterates the profile, updating the plot after each iteration, until it converges or the maximum number of
// iterations from the preferences is reached. The user can continue iterating. Changes of the calculation made here
// start the iterations again, changes of the binning, normalization and averages only update the profile. Changes
// made elsewhere, to the selections or the settings, mark the profile as out of date until it is recalculated.
class DepthProfileView : public QWidget {
    Q_OBJECT

public:
    DepthProfileView(Measurement *measurement, DepthProfile *profile, QWidget *parent = nullptr);
    Measurement *measurement() const { return m_measurement; }
    DepthProfile *profile() const { return m_profile; }
    QString title() const; // Measurement and profile names, marked when out of date
    bool isStale() const { return m_stale; }

public slots:
    void recalculate(); // Iterates from the beginning
    void checkStale(); // Call after the project has changed, also updates the selection summary
    void applyPreferences(); // Call after the preferences have changed

signals:
    void modified();
    void titleChanged();

private slots:
    void iterateOrStop();
    void iterationStep();
    void chooseSelections(); // In a dialog
    void averageItemChanged(QTreeWidgetItem *item, int column);
    void markerMoved(int index, double x);
    void exportProfile();

private:
    enum class DepthUnit { ThinFilm, Mass, Length };
    enum class Interval { Normalization, Average };

    QWidget *createToolBar();
    QWidget *createStaleBar();
    QWidget *createSidePanel();
    void updateSelectionSummary();
    void updateIntervalSpins();
    void startIterating();
    void stopIterating();
    void updateProfile(SpectrumPlotWidget::ViewChange viewChange);
    void updateAverages();
    void updateIterationStatus();
    void updateLegendPosition(); // From the profile or the preferences
    void setStale(bool stale);
    void setInterval(Interval interval, double min, double max); // Stored in the profile, depths in atoms/m²
    void setAutoNormalization(bool automatic); // A manual interval starts from the automatic one
    double toDisplay(double depth) const; // Depth in atoms/m² in the selected unit
    double fromDisplay(double value) const;
    QString depthUnitText() const;

    Measurement *m_measurement;
    DepthProfile *m_profile;
    std::vector<ToFEEvent> m_events;
    DepthProfileInputs m_calculatedInputs;
    std::unique_ptr<DepthProfileCalculation> m_calculation;
    DepthProfileResult m_result;
    bool m_stale = false;
    bool m_iterating = false;
    int m_remainingIterations = 0;
    DepthUnit m_unit = DepthUnit::ThinFilm;
    QList<Interval> m_markerIntervals; // Interval of each pair of plot markers
    SpectrumPlotWidget *m_plot = nullptr;
    QLabel *m_selectionSummary = nullptr; // Selections included in the calculation
    MeasurementRangeWidget *m_rangeWidget = nullptr;
    QCheckBox *m_autoMaxDepthCheck = nullptr;
    QTimer *m_iterationTimer = nullptr;
    QDoubleSpinBox *m_stoppingBinSpin = nullptr;
    QDoubleSpinBox *m_maxDepthSpin = nullptr;
    QSpinBox *m_pseudoEventsSpin = nullptr;
    QDoubleSpinBox *m_binWidthSpin = nullptr;
    QDoubleSpinBox *m_densitySpin = nullptr; // Shown only with depths in nm
    QLabel *m_densityLabel = nullptr;
    QCheckBox *m_autoNormalizationCheck = nullptr;
    QDoubleSpinBox *m_normalizationMinSpin = nullptr;
    QDoubleSpinBox *m_normalizationMaxSpin = nullptr;
    QDoubleSpinBox *m_averageMinSpin = nullptr;
    QDoubleSpinBox *m_averageMaxSpin = nullptr;
    QCheckBox *m_followNormalizationCheck = nullptr;
    QTreeWidget *m_averageTable = nullptr;
    QPushButton *m_iterateButton = nullptr;
    QActionGroup *m_legendGroup = nullptr; // Of the legend positions in the context menu of the plot
    QWidget *m_staleBar = nullptr;
    QLabel *m_errorLabel = nullptr;
    QLabel *m_warningLabel = nullptr; // Elements counted more than once
    QLabel *m_statusLabel = nullptr;
    QLabel *m_iterationLabel = nullptr;
    QString m_hint;
};

#endif // JARA_DEPTHPROFILEVIEW_H
