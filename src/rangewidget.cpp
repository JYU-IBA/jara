/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "rangewidget.h"
#include "project.h"
#include "split.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>

namespace {
constexpr int Steps = 1000; // Of the slider, 0.1 %
constexpr int HandleRadius = 7;
}

// Slider with two handles, values 0...Steps. The handles cannot pass each other.
class RangeSlider : public QWidget {
public:
    explicit RangeSlider(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(2 * HandleRadius + 6);
        setMinimumWidth(100);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    int min() const { return m_min; }
    int max() const { return m_max; }
    void setValues(int min, int max) {
        m_min = std::clamp(min, 0, Steps - 1);
        m_max = std::clamp(max, m_min + 1, Steps);
        update();
    }
    std::function<void(int, int)> moved; // While dragging
    std::function<void(int, int)> released;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const double y = height() / 2.0;
        painter.setPen(QPen(palette().color(QPalette::Mid), 4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(left(), y), QPointF(right(), y));
        painter.setPen(QPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::Highlight), 4,
                            Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(toPixel(m_min), y), QPointF(toPixel(m_max), y));
        painter.setPen(QPen(palette().color(QPalette::Dark), 1));
        painter.setBrush(palette().color(QPalette::Button));
        for(const int value : {m_min, m_max}) {
            painter.drawEllipse(QPointF(toPixel(value), y), HandleRadius, HandleRadius);
        }
    }
    void mousePressEvent(QMouseEvent *event) override {
        const double x = event->position().x();
        // The nearer handle, the upper one when they are on top of each other and the click is to the right
        const double toMin = std::abs(x - toPixel(m_min));
        const double toMax = std::abs(x - toPixel(m_max));
        m_dragged = toMin < toMax || (toMin == toMax && x < toPixel(m_min)) ? Handle::Min : Handle::Max;
        drag(x);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if(m_dragged != Handle::None) {
            drag(event->position().x());
        }
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        if(m_dragged != Handle::None && released) {
            released(m_min, m_max);
        }
        m_dragged = Handle::None;
    }

private:
    enum class Handle { None, Min, Max };
    double left() const { return HandleRadius + 1; }
    double right() const { return width() - HandleRadius - 1; }
    double toPixel(int value) const { return left() + (right() - left()) * value / Steps; }
    int toValue(double x) const {
        return static_cast<int>(std::lround(std::clamp((x - left()) / (right() - left()), 0.0, 1.0) * Steps));
    }
    void drag(double x) {
        const int value = toValue(x);
        if(m_dragged == Handle::Min) {
            m_min = std::min(value, m_max - 1);
        } else {
            m_max = std::max(value, m_min + 1);
        }
        update();
        if(moved) {
            moved(m_min, m_max);
        }
    }

    int m_min = 0;
    int m_max = Steps;
    Handle m_dragged = Handle::None;
};

MeasurementRangeWidget::MeasurementRangeWidget(const std::vector<ToFEEvent> *events, const Measurement *measurement,
                                               QWidget *parent)
    : QWidget(parent), m_events(events), m_measurement(measurement) {
    m_slider = new RangeSlider;
    m_slider->setToolTip("Part of the measurement used, by the counts of the selection that divides the measurement");
    auto percentSpin = []() {
        auto *spin = new QDoubleSpinBox;
        spin->setRange(0, 100);
        spin->setDecimals(1);
        spin->setSuffix(" %");
        spin->setKeyboardTracking(false); // Changed when the value is entered, not on every key
        return spin;
    };
    m_minSpin = percentSpin();
    m_maxSpin = percentSpin();
    m_infoLabel = new QLabel;
    m_infoLabel->setWordWrap(true);

    auto *row = new QHBoxLayout;
    row->addWidget(m_minSpin);
    row->addWidget(m_slider, 1);
    row->addWidget(m_maxSpin);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(row);
    layout->addWidget(m_infoLabel);

    m_slider->moved = [this](int min, int max) { showRange(min, max); };
    m_slider->released = [this](int min, int max) { emit rangeEdited(static_cast<double>(min) / Steps, static_cast<double>(max) / Steps); };
    // The handles cannot pass each other, the spin boxes show where they stopped
    connect(m_minSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        m_slider->setValues(std::min(static_cast<int>(std::lround(value * Steps / 100)), m_slider->max() - 1), m_slider->max());
        showRange(m_slider->min(), m_slider->max());
        emit rangeEdited(min(), max());
    });
    connect(m_maxSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        m_slider->setValues(m_slider->min(), std::max(static_cast<int>(std::lround(value * Steps / 100)), m_slider->min() + 1));
        showRange(m_slider->min(), m_slider->max());
        emit rangeEdited(min(), max());
    });
    m_division = std::make_unique<EventDivision>(*m_events, m_measurement->referencePolygon());
    setRange(0, 1);
}

MeasurementRangeWidget::~MeasurementRangeWidget() = default;

void MeasurementRangeWidget::setRange(double min, double max) {
    m_slider->setValues(static_cast<int>(std::lround(min * Steps)), static_cast<int>(std::lround(max * Steps)));
    showRange(m_slider->min(), m_slider->max());
}

double MeasurementRangeWidget::min() const {
    return static_cast<double>(m_slider->min()) / Steps;
}

double MeasurementRangeWidget::max() const {
    return static_cast<double>(m_slider->max()) / Steps;
}

void MeasurementRangeWidget::setOuterRange(double min, double max) {
    m_outerMin = min;
    m_outerMax = max;
    m_hasOuterRange = true;
    updateInfo();
}

void MeasurementRangeWidget::refresh() {
    m_division = std::make_unique<EventDivision>(*m_events, m_measurement->referencePolygon());
    updateInfo();
}

void MeasurementRangeWidget::showRange(int min, int max) {
    const QSignalBlocker minBlocker(m_minSpin);
    const QSignalBlocker maxBlocker(m_maxSpin);
    m_minSpin->setValue(100.0 * min / Steps);
    m_maxSpin->setValue(100.0 * max / Steps);
    updateInfo();
}

void MeasurementRangeWidget::updateInfo() {
    const Cut *reference = m_measurement->referenceCut();
    // Fractions of the measurement
    const double width = m_outerMax - m_outerMin;
    const double absoluteMin = m_outerMin + min() * width;
    const double absoluteMax = m_outerMin + max() * width;
    const size_t first = m_division->eventIndex(absoluteMin);
    const size_t last = std::max(first, m_division->eventIndex(absoluteMax));
    const size_t counts = m_division->referenceCountsIn(absoluteMin, absoluteMax);
    QString text = QString("%1 of %2 counts of %3, %4 events")
                           .arg(counts)
                           .arg(m_hasOuterRange ? m_division->referenceCountsIn(m_outerMin, m_outerMax)
                                                : m_division->referenceCounts())
                           .arg(reference ? cutLabel(*reference) : QStringLiteral("all events"))
                           .arg(last - first);
    if(m_hasOuterRange) {
        const size_t outerCounts = m_division->referenceCountsIn(m_outerMin, m_outerMax);
        text += counts > 0 ? QString(", weight %1").arg(static_cast<double>(outerCounts) / static_cast<double>(counts), 0, 'g', 4)
                           : QStringLiteral(", no reference counts");
    }
    m_infoLabel->setText(text);
}
