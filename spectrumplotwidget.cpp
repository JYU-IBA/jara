#include "spectrumplotwidget.h"
#include "defaults.h"

#include <algorithm>
#include <cmath>

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QRubberBand>
#include <QWheelEvent>

namespace {
constexpr int MarginLeft = 70;
constexpr int MarginRight = 20;
constexpr int MarginTop = 30;
constexpr int MarginBottom = 50;
constexpr double LogFloor = 0.5; // Counts, bottom of the full range on a log scale, zero is drawn below it
constexpr double ZeroOnLogScale = 0.1;

double niceStep(double range, int targetTicks) {
    const double raw = range / targetTicks;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double r = raw / magnitude;
    const double nice = r < 1.5 ? 1 : r < 3 ? 2 : r < 7 ? 5 : 10;
    return nice * magnitude;
}
}

SpectrumPlotWidget::SpectrumPlotWidget(QWidget *parent)
    : QWidget(parent), m_logScale(Defaults::histogramPreferences().logScale) {
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(300, 200);
    setCursor(Qt::CrossCursor);
}

void SpectrumPlotWidget::setCurves(double binWidth, const QList<Curve> &curves, ViewChange viewChange) {
    m_curves = curves;
    m_binWidth = binWidth > 0 ? binWidth : 1;
    m_bins = 0;
    m_maxCount = 0;
    for(const Curve &curve : curves) {
        m_bins = std::max(m_bins, curve.counts.size());
        for(const size_t count : curve.counts) {
            m_maxCount = std::max(m_maxCount, count);
        }
    }
    updateFullRange();
    switch(viewChange) {
        case ViewChange::ShowAll:
            resetZoom();
            break;
        case ViewChange::KeepEnergyRange:
            setView(m_viewX, m_fullY);
            break;
        case ViewChange::Keep:
            setView(m_viewX, m_viewY);
            break;
    }
}

void SpectrumPlotWidget::setStale(bool stale) {
    m_stale = stale;
    update();
}

void SpectrumPlotWidget::setLogScale(bool logScale) {
    if(logScale == m_logScale) {
        return;
    }
    m_logScale = logScale;
    // The y view is in different units on the two scales, the x view is kept
    updateFullRange();
    setView(m_viewX, m_fullY);
    emit logScaleChanged(logScale);
}

void SpectrumPlotWidget::setMode(SpectrumPlotWidget::Mode mode) {
    m_mode = mode;
    m_rubberBand->hide();
    m_moving = false;
    setCursor(mode == Mode::Move ? Qt::OpenHandCursor : Qt::CrossCursor);
}

void SpectrumPlotWidget::updateFullRange() {
    m_fullX = {0, std::max(1, static_cast<int>(m_bins)) * m_binWidth};
    const double top = static_cast<double>(std::max<size_t>(m_maxCount, 1));
    m_fullY = m_logScale ? Range{std::log10(LogFloor), std::log10(top) + 0.1} : Range{0, top * 1.05};
}

void SpectrumPlotWidget::resetZoom() {
    setView(m_fullX, m_fullY);
}

void SpectrumPlotWidget::zoomIn() {
    zoomAt({(m_viewX.min + m_viewX.max) / 2, (m_viewY.min + m_viewY.max) / 2}, 0.5);
}

void SpectrumPlotWidget::zoomOut() {
    zoomAt({(m_viewX.min + m_viewX.max) / 2, (m_viewY.min + m_viewY.max) / 2}, 2.0);
}

void SpectrumPlotWidget::zoomAt(const QPointF &anchor, double factor) {
    auto zoomed = [factor](const Range &view, const Range &full, double anchor) -> Range {
        const double size = view.size() * factor;
        if(size >= full.size()) {
            return full;
        }
        const double min = anchor - (anchor - view.min) * factor;
        return {min, min + size};
    };
    setView(zoomed(m_viewX, m_fullX, anchor.x()), zoomed(m_viewY, m_fullY, anchor.y()));
}

void SpectrumPlotWidget::moveByPixels(const QPointF &delta) {
    // The content follows the fingers
    const QRect plot = plotRect();
    const double dx = -delta.x() * m_viewX.size() / plot.width();
    const double dy = delta.y() * m_viewY.size() / plot.height();
    setView({m_viewX.min + dx, m_viewX.max + dx}, {m_viewY.min + dy, m_viewY.max + dy});
}

void SpectrumPlotWidget::setView(const Range &x, const Range &y) {
    // Never show areas outside the full range: shrink to it, then shift back inside. At least one bin is shown.
    auto clamped = [](Range view, const Range &full, double minimumSize) -> Range {
        const double size = std::clamp(view.size(), std::min(minimumSize, full.size()), full.size());
        const double min = std::clamp(view.min, full.min, full.max - size);
        return {min, min + size};
    };
    m_viewX = clamped(x, m_fullX, m_binWidth);
    m_viewY = clamped(y, m_fullY, m_logScale ? 0.1 : 1.0);
    update();
}

void SpectrumPlotWidget::wheelEvent(QWheelEvent *event) {
    // Trackpads report scroll phases, mouse wheels do not. On macOS, Qt uses the same device for mice and trackpads
    // and marks it as a touchpad once a trackpad has scrolled, so the device type is not used there.
    bool trackpad = event->phase() != Qt::NoScrollPhase;
#ifndef Q_OS_MACOS
    trackpad = trackpad || (event->pointingDevice() && event->pointingDevice()->type() == QInputDevice::DeviceType::TouchPad);
#endif
    if(trackpad && !(event->modifiers() & Qt::ControlModifier)) {
        moveByPixels(event->pixelDelta().isNull() ? QPointF(event->angleDelta()) / 8.0 : QPointF(event->pixelDelta()));
    } else {
        const double steps = event->angleDelta().y() / 120.0; // One step per wheel notch
        if(steps == 0) {
            event->ignore();
            return;
        }
        zoomAt(pixelToData(event->position()), std::pow(0.8, steps));
    }
    updateCursorInfo(event->position().toPoint());
    event->accept();
}

bool SpectrumPlotWidget::event(QEvent *event) {
    if(event->type() == QEvent::NativeGesture) {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        if(gesture->gestureType() == Qt::ZoomNativeGesture) {
            // Qt passes the gesture on from the window without mapping position() to this widget
            const QPointF pos = mapFromGlobal(gesture->globalPosition());
            const double scale = 1.0 + std::clamp(gesture->value(), -0.5, 0.5); // Pinching out gives positive values
            zoomAt(pixelToData(pos), 1.0 / scale);
            updateCursorInfo(pos.toPoint());
            return true;
        }
        if(gesture->gestureType() == Qt::SmartZoomNativeGesture) {
            resetZoom();
            return true;
        }
    }
    return QWidget::event(event);
}

QRect SpectrumPlotWidget::plotRect() const {
    return rect().adjusted(MarginLeft, MarginTop, -MarginRight, -MarginBottom);
}

double SpectrumPlotWidget::toPlotY(double count) const {
    if(!m_logScale) {
        return count;
    }
    return std::log10(count > 0 ? count : ZeroOnLogScale);
}

QPointF SpectrumPlotWidget::dataToPixel(double x, double y) const {
    const QRect plot = plotRect();
    return {plot.left() + (x - m_viewX.min) / m_viewX.size() * plot.width(),
            plot.top() + plot.height() - (y - m_viewY.min) / m_viewY.size() * plot.height()};
}

QPointF SpectrumPlotWidget::pixelToData(const QPointF &pixel) const {
    const QRect plot = plotRect();
    return {m_viewX.min + (pixel.x() - plot.left()) / plot.width() * m_viewX.size(),
            m_viewY.min + (plot.top() + plot.height() - pixel.y()) / plot.height() * m_viewY.size()};
}

void SpectrumPlotWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    const QRect plot = plotRect();
    if(plot.width() <= 0 || plot.height() <= 0) {
        return;
    }
    painter.fillRect(plot, Qt::white);
    painter.save();
    painter.setClipRect(plot);
    for(const Curve &curve : m_curves) {
        drawCurve(painter, curve);
    }
    painter.restore();
    drawAxes(painter, plot);
    drawLegend(painter, plot);
}

void SpectrumPlotWidget::drawCurve(QPainter &painter, const Curve &curve) const {
    if(curve.counts.empty()) {
        return;
    }
    const auto first = static_cast<size_t>(std::clamp(std::floor(m_viewX.min / m_binWidth), 0.0, static_cast<double>(curve.counts.size() - 1)));
    const auto last = static_cast<size_t>(std::clamp(std::ceil(m_viewX.max / m_binWidth), 0.0, static_cast<double>(curve.counts.size() - 1)));
    const double bottom = m_logScale ? std::log10(ZeroOnLogScale) : 0;
    QPolygonF steps;
    steps.append(dataToPixel(static_cast<double>(first) * m_binWidth, bottom));
    const double binsPerPixel = static_cast<double>(last - first + 1) / plotRect().width();
    if(binsPerPixel <= 2) {
        for(size_t i = first; i <= last; i++) {
            const double y = toPlotY(static_cast<double>(curve.counts[i]));
            steps.append(dataToPixel(static_cast<double>(i) * m_binWidth, y));
            steps.append(dataToPixel(static_cast<double>(i + 1) * m_binWidth, y));
        }
    } else {
        // Many bins per pixel: the range of counts within each pixel column is drawn as a vertical line
        const auto perColumn = static_cast<size_t>(std::ceil(binsPerPixel));
        for(size_t i = first; i <= last; i += perColumn) {
            const size_t end = std::min(last + 1, i + perColumn);
            const auto [min, max] = std::minmax_element(curve.counts.begin() + static_cast<std::ptrdiff_t>(i),
                                                        curve.counts.begin() + static_cast<std::ptrdiff_t>(end));
            const double x = (static_cast<double>(i + end) / 2) * m_binWidth;
            steps.append(dataToPixel(x, toPlotY(static_cast<double>(*min))));
            steps.append(dataToPixel(x, toPlotY(static_cast<double>(*max))));
        }
    }
    steps.append(dataToPixel(static_cast<double>(last + 1) * m_binWidth, bottom));
    QColor color = curve.color;
    if(m_stale) {
        color.setAlpha(90);
    }
    QPen pen(color, 1.5);
    if(m_stale) {
        pen.setStyle(Qt::DashLine);
    }
    painter.setPen(pen);
    painter.drawPolyline(steps);
}

void SpectrumPlotWidget::drawAxes(QPainter &painter, const QRect &plot) const {
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawRect(plot.adjusted(0, 0, -1, -1));
    const QFontMetrics metrics = painter.fontMetrics();

    const double stepX = niceStep(m_viewX.size(), std::max(2, plot.width() / 80));
    for(double x = std::ceil(m_viewX.min / stepX) * stepX; x <= m_viewX.max; x += stepX) {
        const int px = static_cast<int>(std::round(dataToPixel(x, 0).x()));
        painter.drawLine(px, plot.bottom(), px, plot.bottom() + 5);
        const QString label = QString::number(x, 'g', 6);
        painter.drawText(px - metrics.horizontalAdvance(label) / 2, plot.bottom() + 6 + metrics.ascent(), label);
    }
    auto drawYTick = [&](double y, const QString &label) {
        const int py = static_cast<int>(std::round(dataToPixel(0, y).y()));
        painter.drawLine(plot.left() - 5, py, plot.left(), py);
        painter.drawText(plot.left() - 8 - metrics.horizontalAdvance(label), py + metrics.ascent() / 2, label);
    };
    if(m_logScale) {
        // Decades, and 2 and 5 times them when few decades are shown
        const bool fewDecades = m_viewY.size() < 2.5;
        for(double decade = std::floor(m_viewY.min); decade <= m_viewY.max; decade++) {
            for(const double factor : {1.0, 2.0, 5.0}) {
                const double y = decade + std::log10(factor);
                if((factor == 1 || fewDecades) && y >= m_viewY.min && y <= m_viewY.max) {
                    drawYTick(y, QString::number(std::pow(10.0, y), 'g', 6));
                }
            }
        }
    } else {
        const double stepY = std::max(1.0, niceStep(m_viewY.size(), std::max(2, plot.height() / 50)));
        for(double y = std::ceil(m_viewY.min / stepY) * stepY; y <= m_viewY.max; y += stepY) {
            drawYTick(y, QString::number(y, 'g', 10));
        }
    }

    const QString xTitle = QStringLiteral("E (MeV)");
    painter.drawText(plot.center().x() - metrics.horizontalAdvance(xTitle) / 2, height() - 8, xTitle);
    const QString yTitle = QStringLiteral("Counts");
    painter.save();
    painter.translate(14, plot.center().y() + metrics.horizontalAdvance(yTitle) / 2);
    painter.rotate(-90);
    painter.drawText(0, 0, yTitle);
    painter.restore();

    if(!m_curves.isEmpty()) {
        painter.drawText(plot.left(), MarginTop - 10,
                         QString("Bin %1 keV, max %2 counts/bin, %3 scale")
                                 .arg(m_binWidth * 1000, 0, 'g', 6).arg(m_maxCount).arg(m_logScale ? "log" : "linear"));
    }
}

void SpectrumPlotWidget::drawLegend(QPainter &painter, const QRect &plot) const {
    if(m_curves.isEmpty()) {
        return;
    }
    const QFontMetrics metrics = painter.fontMetrics();
    constexpr int Padding = 6;
    constexpr int LineLength = 20;
    QStringList labels;
    int textWidth = 0;
    for(const Curve &curve : m_curves) {
        labels.append(QString("%1 — %2 events").arg(curve.name).arg(curve.events));
        textWidth = std::max(textWidth, metrics.horizontalAdvance(labels.last()));
    }
    const int lineHeight = metrics.height();
    const QRect box(plot.right() - Padding * 3 - LineLength - textWidth - 10, plot.top() + 10,
                    Padding * 3 + LineLength + textWidth, Padding * 2 + lineHeight * static_cast<int>(m_curves.size()));
    painter.save();
    painter.setPen(palette().color(QPalette::Mid));
    painter.setBrush(QColor(255, 255, 255, 220));
    painter.drawRect(box);
    for(int i = 0; i < m_curves.size(); i++) {
        const int y = box.top() + Padding + i * lineHeight;
        painter.setPen(QPen(m_curves[i].color, 2));
        painter.drawLine(box.left() + Padding, y + lineHeight / 2, box.left() + Padding + LineLength, y + lineHeight / 2);
        painter.setPen(Qt::black);
        painter.drawText(box.left() + Padding * 2 + LineLength, y + metrics.ascent(), labels[i]);
    }
    painter.restore();
}

void SpectrumPlotWidget::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu;
    QAction *log = menu.addAction("Logarithmic Scale", this, [this]() { setLogScale(true); });
    QAction *linear = menu.addAction("Linear Scale", this, [this]() { setLogScale(false); });
    log->setCheckable(true);
    log->setChecked(m_logScale);
    linear->setCheckable(true);
    linear->setChecked(!m_logScale);
    menu.addSeparator();
    menu.addAction("Show All", this, &SpectrumPlotWidget::resetZoom);
    menu.exec(event->globalPos());
}

void SpectrumPlotWidget::mousePressEvent(QMouseEvent *event) {
    if(event->button() != Qt::LeftButton || !plotRect().contains(event->pos())) {
        return;
    }
    m_dragStart = event->pos();
    if(m_mode == Mode::Zoom) {
        m_rubberBand->setGeometry(QRect(m_dragStart, QSize()));
        m_rubberBand->show();
    } else {
        m_moveStartX = m_viewX;
        m_moveStartY = m_viewY;
        m_moving = true;
        setCursor(Qt::ClosedHandCursor);
    }
}

void SpectrumPlotWidget::mouseMoveEvent(QMouseEvent *event) {
    if(m_rubberBand->isVisible()) {
        m_rubberBand->setGeometry(QRect(m_dragStart, event->pos()).normalized() & plotRect());
    }
    if(m_moving) {
        const QRect plot = plotRect();
        const double dx = (m_dragStart.x() - event->pos().x()) * m_moveStartX.size() / plot.width();
        const double dy = (event->pos().y() - m_dragStart.y()) * m_moveStartY.size() / plot.height();
        setView({m_moveStartX.min + dx, m_moveStartX.max + dx}, {m_moveStartY.min + dy, m_moveStartY.max + dy});
    }
    updateCursorInfo(event->pos());
}

void SpectrumPlotWidget::mouseReleaseEvent(QMouseEvent *event) {
    if(event->button() != Qt::LeftButton) {
        return;
    }
    if(m_moving) {
        m_moving = false;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if(!m_rubberBand->isVisible()) {
        return;
    }
    m_rubberBand->hide();
    const QRect selection = m_rubberBand->geometry();
    if(selection.width() < 5 || selection.height() < 5) {
        return;
    }
    const QPointF topLeft = pixelToData(selection.topLeft());
    const QPointF bottomRight = pixelToData(selection.bottomRight());
    setView({topLeft.x(), bottomRight.x()}, {bottomRight.y(), topLeft.y()});
}

void SpectrumPlotWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if(event->button() == Qt::LeftButton && m_mode == Mode::Zoom) {
        resetZoom();
    }
}

void SpectrumPlotWidget::updateCursorInfo(const QPoint &pos) {
    if(!plotRect().contains(pos) || m_curves.isEmpty()) {
        emit cursorInfoChanged({});
        return;
    }
    const double x = pixelToData(pos).x();
    const double bin = std::floor(x / m_binWidth);
    if(bin < 0 || bin >= static_cast<double>(m_bins)) {
        emit cursorInfoChanged({});
        return;
    }
    const auto index = static_cast<size_t>(bin);
    QStringList counts;
    for(const Curve &curve : m_curves) {
        counts.append(QString("%1: %2").arg(curve.name)
                              .arg(index < curve.counts.size() ? curve.counts[index] : 0));
    }
    emit cursorInfoChanged(QString("E %1–%2 MeV   %3")
                                   .arg(bin * m_binWidth, 0, 'f', 4).arg((bin + 1) * m_binWidth, 0, 'f', 4)
                                   .arg(counts.join(QStringLiteral(", "))));
}
