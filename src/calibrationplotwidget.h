/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

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
