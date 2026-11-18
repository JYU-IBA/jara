#ifndef JARA_CALIBRATIONPLOTWIDGET_H
#define JARA_CALIBRATIONPLOTWIDGET_H

#include <QList>
#include <QPointF>
#include <QWidget>

// Calibration points, ToF channel against time of flight in ns, with the fitted line through them
class CalibrationPlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit CalibrationPlotWidget(QWidget *parent = nullptr);
    void setPoints(const QList<QPointF> &points); // Channel, ns
    void setLine(double slope, double offset); // ns/ch and ns
    void clearLine();
    void setHighlighted(int index); // Point drawn larger, -1 for none

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QList<QPointF> m_points;
    bool m_hasLine = false;
    double m_slope = 0;
    double m_offset = 0;
    int m_highlighted = -1;
};

#endif // JARA_CALIBRATIONPLOTWIDGET_H
