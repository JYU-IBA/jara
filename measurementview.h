#ifndef JARA_MEASUREMENTVIEW_H
#define JARA_MEASUREMENTVIEW_H

#include <vector>

#include <QList>
#include <QPointF>
#include <QWidget>

#include "tofedata.h"

struct Measurement;
class HistogramWidget;
class QLabel;
class QListWidget;

// Tab in the main work area where a measurement is worked with. Loads the measurement's data file when created.
class MeasurementView : public QWidget {
    Q_OBJECT

public:
    explicit MeasurementView(Measurement *measurement, QWidget *parent = nullptr);
    Measurement *measurement() const { return m_measurement; }

signals:
    void modified();

private slots:
    void polygonCompleted(const QList<QPointF> &polygon);
    void changeCutElement();
    void removeCut();

private:
    QWidget *createToolBar();
    QWidget *createCutPanel();
    void updateCutList();

    Measurement *m_measurement;
    std::vector<ToFEEvent> m_events;
    HistogramWidget *m_histogram = nullptr;
    QListWidget *m_cutList = nullptr;
    QLabel *m_statusLabel = nullptr;
    QString m_hint;
};

#endif // JARA_MEASUREMENTVIEW_H
