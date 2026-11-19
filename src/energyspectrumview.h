#ifndef JARA_ENERGYSPECTRUMVIEW_H
#define JARA_ENERGYSPECTRUMVIEW_H

#include <vector>

#include <QWidget>

#include "energyspectrum.h"
#include "spectrumplotwidget.h"
#include "tofedata.h"

struct EnergySpectrum;
struct Measurement;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QListWidgetItem;

// Tab in the main work area showing an energy spectrum of a measurement. Loads the measurement's data file when
// created. Changes made here recalculate the spectrum right away. Changes made elsewhere, to the selections or the
// ToF calibration, only mark the spectrum as out of date until it is refreshed.
class EnergySpectrumView : public QWidget {
    Q_OBJECT

public:
    EnergySpectrumView(Measurement *measurement, EnergySpectrum *spectrum, QWidget *parent = nullptr);
    Measurement *measurement() const { return m_measurement; }
    EnergySpectrum *spectrum() const { return m_spectrum; }
    QString title() const; // Measurement and spectrum names, marked when out of date
    bool isStale() const { return m_stale; }

public slots:
    void recalculate();
    void checkStale(); // Call after the project has changed, also updates the selections shown

signals:
    void modified();
    void titleChanged();

private slots:
    void selectionItemChanged(QListWidgetItem *item);
    void exportSpectrum();

private:
    QWidget *createToolBar();
    QWidget *createStaleBar();
    QWidget *createSelectionPanel();
    void updateSelectionList();
    void updateCurves(SpectrumPlotWidget::ViewChange viewChange);
    void setStale(bool stale);

    Measurement *m_measurement;
    EnergySpectrum *m_spectrum;
    std::vector<ToFEEvent> m_events;
    EnergySpectrumInputs m_calculatedInputs;
    EnergySpectrumResult m_result;
    bool m_stale = false;
    SpectrumPlotWidget *m_plot = nullptr;
    QDoubleSpinBox *m_binWidthSpin = nullptr;
    QListWidget *m_selectionList = nullptr;
    QWidget *m_staleBar = nullptr;
    QLabel *m_errorLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QString m_hint;
};

#endif // JARA_ENERGYSPECTRUMVIEW_H
