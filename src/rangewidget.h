#ifndef JARA_RANGEWIDGET_H
#define JARA_RANGEWIDGET_H

#include <memory>
#include <vector>

#include <QWidget>

#include "tofedata.h"

struct Measurement;
class EventDivision;
class QDoubleSpinBox;
class QLabel;
class RangeSlider;

// Part of a measurement used by an energy spectrum or a depth profile, from and to a percentage of the counts of the
// measurement's reference, see split.h. A slider with two handles and spin boxes, with the counts of the reference in
// the range below them.
class MeasurementRangeWidget : public QWidget {
    Q_OBJECT

public:
    MeasurementRangeWidget(const std::vector<ToFEEvent> *events, const Measurement *measurement, QWidget *parent = nullptr);
    ~MeasurementRangeWidget() override;
    void setRange(double min, double max); // Fractions, 0...1
    double min() const;
    double max() const;
    void refresh(); // Call after the reference may have changed

signals:
    void rangeEdited(double min, double max); // When the user has finished changing it

private:
    void showRange(int min, int max); // In the spin boxes and the info, from the slider
    void updateInfo();

    const std::vector<ToFEEvent> *m_events;
    const Measurement *m_measurement;
    std::unique_ptr<EventDivision> m_division; // By the reference when last refreshed
    RangeSlider *m_slider;
    QDoubleSpinBox *m_minSpin; // %
    QDoubleSpinBox *m_maxSpin;
    QLabel *m_infoLabel;
};

#endif // JARA_RANGEWIDGET_H
