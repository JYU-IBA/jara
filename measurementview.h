#ifndef JARA_MEASUREMENTVIEW_H
#define JARA_MEASUREMENTVIEW_H

#include <vector>

#include <QWidget>

#include "tofedata.h"

struct Measurement;

// Tab in the main work area where a measurement is worked with. Loads the measurement's data file when created.
class MeasurementView : public QWidget {
    Q_OBJECT

public:
    explicit MeasurementView(Measurement *measurement, QWidget *parent = nullptr);
    Measurement *measurement() const { return m_measurement; }

private:
    Measurement *m_measurement;
    std::vector<ToFEEvent> m_events;
};

#endif // JARA_MEASUREMENTVIEW_H
