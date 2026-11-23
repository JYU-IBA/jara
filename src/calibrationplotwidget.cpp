/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "calibrationplotwidget.h"
#include "spectrumplotwidget.h"

#include <algorithm>
#include <cmath>

#include <QPainter>

namespace {
constexpr int MarginLeft = 70;
constexpr int MarginRight = 20;
constexpr int MarginTop = 20;
constexpr int MarginBottom = 50;
constexpr int PointRadius = 4;

struct Range {
    double min;
    double max;
    double size() const { return max - min; }
};

// Range of the values with some room on both sides, never empty
Range paddedRange(double min, double max) {
    const double padding = max > min ? 0.1 * (max - min) : std::max(1.0, 0.1 * std::abs(min));
    return {min - padding, max + padding};
}
}

CalibrationPlotWidget::CalibrationPlotWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(300, 200);
}

void CalibrationPlotWidget::setPoints(const QList<QPointF> &points) {
    m_points = points;
    m_highlighted = -1;
    update();
}

void CalibrationPlotWidget::setLine(double slope, double offset) {
    m_hasLine = true;
    m_slope = slope;
    m_offset = offset;
    update();
}

void CalibrationPlotWidget::clearLine() {
    m_hasLine = false;
    update();
}

void CalibrationPlotWidget::setHighlighted(int index) {
    m_highlighted = index;
    update();
}

void CalibrationPlotWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    const QRect plot = rect().adjusted(MarginLeft, MarginTop, -MarginRight, -MarginBottom);
    if(plot.width() <= 0 || plot.height() <= 0) {
        return;
    }
    painter.fillRect(plot, Qt::white);
    const QFontMetrics metrics = painter.fontMetrics();
    if(m_points.isEmpty()) {
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawRect(plot.adjusted(0, 0, -1, -1));
        painter.drawText(plot, Qt::AlignCenter | Qt::TextWordWrap, "Accept the edges of selections to add points.");
        return;
    }

    double minX = m_points.first().x(), maxX = minX;
    double minY = m_points.first().y(), maxY = minY;
    for(const QPointF &point : m_points) {
        minX = std::min(minX, point.x());
        maxX = std::max(maxX, point.x());
        minY = std::min(minY, point.y());
        maxY = std::max(maxY, point.y());
    }
    const Range x = paddedRange(minX, maxX);
    if(m_hasLine) {
        for(const double channel : {x.min, x.max}) {
            minY = std::min(minY, m_slope * channel + m_offset);
            maxY = std::max(maxY, m_slope * channel + m_offset);
        }
    }
    const Range y = paddedRange(minY, maxY);
    auto toPixel = [&](double channel, double tof) {
        return QPointF(plot.left() + (channel - x.min) / x.size() * plot.width(),
                       plot.bottom() - (tof - y.min) / y.size() * plot.height());
    };

    painter.save();
    painter.setClipRect(plot);
    painter.setRenderHint(QPainter::Antialiasing);
    if(m_hasLine) {
        painter.setPen(QPen(QColor(0x1f, 0x77, 0xb4), 1.5));
        painter.drawLine(toPixel(x.min, m_slope * x.min + m_offset), toPixel(x.max, m_slope * x.max + m_offset));
    }
    for(int i = 0; i < m_points.size(); i++) {
        const int radius = i == m_highlighted ? PointRadius + 2 : PointRadius;
        painter.setPen(QPen(Qt::black, 1));
        painter.setBrush(i == m_highlighted ? QColor(0xd6, 0x27, 0x28) : QColor(0xff, 0x7f, 0x0e));
        painter.drawEllipse(toPixel(m_points[i].x(), m_points[i].y()), radius, radius);
    }
    painter.restore();

    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawRect(plot.adjusted(0, 0, -1, -1));
    const double stepX = niceStep(x.size(), std::max(2, plot.width() / 80));
    for(double value = std::ceil(x.min / stepX) * stepX; value <= x.max; value += stepX) {
        const int px = static_cast<int>(std::round(toPixel(value, y.min).x()));
        painter.drawLine(px, plot.bottom(), px, plot.bottom() + 5);
        const QString label = QString::number(value, 'g', 6);
        painter.drawText(px - metrics.horizontalAdvance(label) / 2, plot.bottom() + 6 + metrics.ascent(), label);
    }
    const double stepY = niceStep(y.size(), std::max(2, plot.height() / 50));
    for(double value = std::ceil(y.min / stepY) * stepY; value <= y.max; value += stepY) {
        const int py = static_cast<int>(std::round(toPixel(x.min, value).y()));
        painter.drawLine(plot.left() - 5, py, plot.left(), py);
        const QString label = QString::number(value, 'g', 6);
        painter.drawText(plot.left() - 8 - metrics.horizontalAdvance(label), py + metrics.ascent() / 2, label);
    }
    const QString xTitle = QStringLiteral("ToF (ch)");
    painter.drawText(plot.center().x() - metrics.horizontalAdvance(xTitle) / 2, height() - 8, xTitle);
    const QString yTitle = QStringLiteral("ToF (ns)");
    painter.save();
    painter.translate(14, plot.center().y() + metrics.horizontalAdvance(yTitle) / 2);
    painter.rotate(-90);
    painter.drawText(0, 0, yTitle);
    painter.restore();
}
