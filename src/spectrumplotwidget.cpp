#include "spectrumplotwidget.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

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
constexpr double ZeroOnLogScale = 0.2; // Zero values on a log scale, relative to the floor of the y axis
constexpr int MarkerGrabDistance = 4; // Pixels
}

double niceStep(double range, int targetTicks) {
    const double raw = range / targetTicks;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double r = raw / magnitude;
    const double nice = r < 1.5 ? 1 : r < 3 ? 2 : r < 7 ? 5 : 10;
    return nice * magnitude;
}

SpectrumPlotWidget::SpectrumPlotWidget(QWidget *parent)
    : QWidget(parent) {
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(300, 200);
    setCursor(Qt::CrossCursor);
}

void SpectrumPlotWidget::setXAxis(const XAxis &axis) {
    m_xAxis = axis;
    updateFullRange();
    update();
}

void SpectrumPlotWidget::setYAxis(const YAxis &axis) {
    m_yAxis = axis;
    updateFullRange();
    update();
}

void SpectrumPlotWidget::setBinEdges(const std::vector<double> &edges) {
    m_edges = edges.size() >= 2 ? edges : std::vector<double>();
}

void SpectrumPlotWidget::setRegions(const QList<Region> &regions) {
    m_regions = regions;
    update();
}

void SpectrumPlotWidget::setInfoText(const QString &text) {
    m_infoText = text;
    update();
}

void SpectrumPlotWidget::setLegendCorner(Qt::Corner corner) {
    m_legendCorner = corner;
    update();
}

void SpectrumPlotWidget::setContextActions(const QList<QAction *> &actions) {
    m_contextActions = actions;
}

double SpectrumPlotWidget::binStart(size_t bin) const {
    if(!m_edges.empty()) {
        return m_edges[std::min(bin, m_edges.size() - 1)];
    }
    return m_xAxis.origin + static_cast<double>(bin) * m_binWidth;
}

int SpectrumPlotWidget::binAt(double x) const {
    if(!m_edges.empty()) {
        if(x < m_edges.front() || x >= m_edges.back()) {
            return -1;
        }
        return static_cast<int>(std::upper_bound(m_edges.begin(), m_edges.end(), x) - m_edges.begin()) - 1;
    }
    const double bin = std::floor((x - m_xAxis.origin) / m_binWidth);
    return bin < 0 || bin >= static_cast<double>(m_bins) ? -1 : static_cast<int>(bin);
}

void SpectrumPlotWidget::setOverlay(const QList<QPointF> &points, const QColor &color) {
    m_overlay = points;
    m_overlayColor = color;
    update();
}

void SpectrumPlotWidget::setMarkers(const QList<Marker> &markers) {
    m_markers = markers;
    m_draggedMarker = -1;
    update();
}

void SpectrumPlotWidget::setXView(double min, double max) {
    setView({min, max}, m_viewY);
}

void SpectrumPlotWidget::setCurves(double binWidth, const QList<Curve> &curves, ViewChange viewChange) {
    m_curves = curves;
    m_binWidth = binWidth > 0 ? binWidth : 1;
    m_bins = 0;
    m_maxValue = 0;
    m_minValue = std::numeric_limits<double>::infinity();
    for(const Curve &curve : curves) {
        m_bins = std::max(m_bins, curve.values.size());
        for(size_t i = 0; i < curve.values.size(); i++) {
            const double error = i < curve.errors.size() ? curve.errors[i] : 0;
            m_maxValue = std::max(m_maxValue, curve.values[i] + error);
            m_minValue = std::min(m_minValue, curve.values[i] - error);
        }
    }
    if(!m_edges.empty()) {
        m_bins = std::min(m_bins, m_edges.size() - 1);
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
    m_draggedMarker = -1;
    setCursor(mode == Mode::Move ? Qt::OpenHandCursor : mode == Mode::Pick ? Qt::PointingHandCursor : Qt::CrossCursor);
}

void SpectrumPlotWidget::updateFullRange() {
    if(!m_edges.empty()) {
        m_fullX = {m_edges.front(), m_edges.back()};
    } else {
        m_fullX = {m_xAxis.origin, m_xAxis.origin + std::max(1, static_cast<int>(m_bins)) * m_binWidth};
    }
    const double floor = m_yAxis.logFloor;
    const double top = std::max(m_maxValue, 2 * floor);
    if(!m_logScale && !m_yAxis.includeZero && std::isfinite(m_minValue) && m_maxValue > 0) {
        // A tenth of the range of the values as margins, or of the values themselves when they are all the same
        const double margin = 0.1 * (m_maxValue > m_minValue ? m_maxValue - m_minValue : m_maxValue);
        m_fullY = {m_minValue - margin, m_maxValue + margin};
        return;
    }
    m_fullY = m_logScale ? Range{std::log10(floor), std::log10(top) + 0.1} : Range{0, top * 1.05};
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
        // Not std::clamp: full.max - size can be a rounding error below full.min
        const double min = std::max(full.min, std::min(view.min, full.max - size));
        return {min, min + size};
    };
    const double minBinSize = m_edges.empty() ? m_binWidth : (m_edges.back() - m_edges.front()) / static_cast<double>(m_edges.size() - 1);
    m_viewX = clamped(x, m_fullX, minBinSize);
    m_viewY = clamped(y, m_fullY, m_logScale ? 0.1 : std::min(1.0, m_fullY.size() / 100));
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
    return std::log10(count > 0 ? count : ZeroOnLogScale * m_yAxis.logFloor);
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
    drawRegions(painter, plot);
    for(const Curve &curve : m_curves) {
        drawCurve(painter, curve);
    }
    drawOverlay(painter);
    drawMarkers(painter, plot);
    painter.restore();
    drawAxes(painter, plot);
    drawLegend(painter, plot);
}

void SpectrumPlotWidget::drawCurve(QPainter &painter, const Curve &curve) const {
    const size_t bins = std::min(curve.values.size(), m_bins);
    if(bins == 0) {
        return;
    }
    // The bins in view
    const int firstInView = binAt(m_viewX.min);
    const int lastInView = binAt(m_viewX.max);
    const size_t first = firstInView < 0 ? (m_viewX.min < binStart(0) ? 0 : bins - 1) : static_cast<size_t>(firstInView);
    const size_t last = std::min(bins - 1, lastInView < 0 ? (m_viewX.max < binStart(0) ? 0 : bins - 1) : static_cast<size_t>(lastInView));
    const double bottom = m_logScale ? std::log10(ZeroOnLogScale * m_yAxis.logFloor) : 0;
    QPolygonF steps;
    steps.append(dataToPixel(binStart(first), bottom));
    const double binsPerPixel = static_cast<double>(last - first + 1) / plotRect().width();
    if(binsPerPixel <= 2) {
        for(size_t i = first; i <= last; i++) {
            const double y = toPlotY(curve.values[i]);
            steps.append(dataToPixel(binStart(i), y));
            steps.append(dataToPixel(binStart(i + 1), y));
        }
    } else {
        // Many bins per pixel: the range of values within each pixel column is drawn as a vertical line
        const auto perColumn = static_cast<size_t>(std::ceil(binsPerPixel));
        for(size_t i = first; i <= last; i += perColumn) {
            const size_t end = std::min(last + 1, i + perColumn);
            const auto [min, max] = std::minmax_element(curve.values.begin() + static_cast<std::ptrdiff_t>(i),
                                                        curve.values.begin() + static_cast<std::ptrdiff_t>(end));
            const double x = (binStart(i) + binStart(end)) / 2;
            steps.append(dataToPixel(x, toPlotY(*min)));
            steps.append(dataToPixel(x, toPlotY(*max)));
        }
    }
    steps.append(dataToPixel(binStart(last + 1), bottom));
    QColor color = curve.color;
    if(m_stale) {
        color.setAlpha(90);
    }
    QPen pen(color, 1.5, curve.style);
    if(m_stale) {
        pen.setStyle(Qt::DashLine);
    }
    painter.setPen(pen);
    painter.drawPolyline(steps);
    if(!curve.errors.empty()) {
        painter.setPen(QPen(color, 1));
        for(size_t i = first; i <= last && i < curve.errors.size(); i++) {
            const double x = (binStart(i) + binStart(i + 1)) / 2;
            painter.drawLine(dataToPixel(x, toPlotY(curve.values[i] - curve.errors[i])),
                             dataToPixel(x, toPlotY(curve.values[i] + curve.errors[i])));
        }
    }
    if(curve.line.size() >= 2) {
        QPolygonF line;
        for(const QPointF &point : curve.line) {
            line.append(dataToPixel(point.x(), toPlotY(point.y())));
        }
        painter.setPen(QPen(color, 1, Qt::DashLine));
        painter.drawPolyline(line);
    }
}

void SpectrumPlotWidget::drawRegions(QPainter &painter, const QRect &plot) const {
    for(const Region &region : m_regions) {
        const double left = dataToPixel(region.min, 0).x();
        const double right = dataToPixel(region.max, 0).x();
        painter.fillRect(QRectF(QPointF(left, plot.top()), QPointF(right, plot.bottom())), region.color);
    }
}

void SpectrumPlotWidget::drawOverlay(QPainter &painter) const {
    if(m_overlay.size() < 2) {
        return;
    }
    QPolygonF line;
    for(const QPointF &point : m_overlay) {
        line.append(dataToPixel(point.x(), toPlotY(point.y())));
    }
    painter.setPen(QPen(m_overlayColor, 2));
    painter.drawPolyline(line);
}

void SpectrumPlotWidget::drawMarkers(QPainter &painter, const QRect &plot) const {
    for(const Marker &marker : m_markers) {
        const double x = std::round(dataToPixel(marker.x, 0).x());
        painter.setPen(QPen(marker.color, marker.draggable ? 2 : 1.5, marker.style));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
}

int SpectrumPlotWidget::markerAt(const QPoint &pos) const {
    if(m_mode == Mode::Move || !plotRect().contains(pos)) {
        return -1;
    }
    for(int i = 0; i < m_markers.size(); i++) {
        if(m_markers[i].draggable && std::abs(dataToPixel(m_markers[i].x, 0).x() - pos.x()) <= MarkerGrabDistance) {
            return i;
        }
    }
    return -1;
}

void SpectrumPlotWidget::updateIdleCursor(const QPoint &pos) {
    if(m_mode == Mode::Move) {
        return;
    }
    if(markerAt(pos) >= 0) {
        setCursor(Qt::SizeHorCursor);
    } else {
        setCursor(m_mode == Mode::Pick ? Qt::PointingHandCursor : Qt::CrossCursor);
    }
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

    const QString &xTitle = m_xAxis.title;
    painter.drawText(plot.center().x() - metrics.horizontalAdvance(xTitle) / 2, height() - 8, xTitle);
    const QString &yTitle = m_yAxis.title;
    painter.save();
    painter.translate(14, plot.center().y() + metrics.horizontalAdvance(yTitle) / 2);
    painter.rotate(-90);
    painter.drawText(0, 0, yTitle);
    painter.restore();

    if(!m_infoText.isEmpty()) {
        painter.drawText(plot.left(), MarginTop - 10, m_infoText);
    } else if(!m_curves.isEmpty()) {
        painter.drawText(plot.left(), MarginTop - 10,
                         QString("Bin %1 %2, max %3 %4, %5 scale")
                                 .arg(m_binWidth * m_xAxis.binWidthScale, 0, 'g', 6).arg(m_xAxis.binWidthUnit)
                                 .arg(m_maxValue, 0, 'g', 6).arg(m_yAxis.maxUnit).arg(m_logScale ? "log" : "linear"));
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
        labels.append((m_yAxis.legendEvents ? QString("%1 — %2 events").arg(curve.name).arg(curve.events) : curve.name)
                      + curve.legendSuffix);
        textWidth = std::max(textWidth, metrics.horizontalAdvance(labels.last()));
    }
    const int lineHeight = metrics.height();
    const int width = Padding * 3 + LineLength + textWidth;
    const QRect box(m_legendCorner == Qt::TopLeftCorner ? plot.left() + 10 : plot.right() - width - 10, plot.top() + 10,
                    width, Padding * 2 + lineHeight * static_cast<int>(m_curves.size()));
    painter.save();
    painter.setPen(palette().color(QPalette::Mid));
    painter.setBrush(QColor(255, 255, 255, 220));
    painter.drawRect(box);
    for(int i = 0; i < m_curves.size(); i++) {
        const int y = box.top() + Padding + i * lineHeight;
        painter.setPen(QPen(m_curves[i].color, 2, m_curves[i].style));
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
    if(!m_contextActions.isEmpty()) {
        menu.addSeparator();
        menu.addActions(m_contextActions);
    }
    menu.exec(event->globalPos());
}

void SpectrumPlotWidget::mousePressEvent(QMouseEvent *event) {
    if(event->button() != Qt::LeftButton || !plotRect().contains(event->pos())) {
        return;
    }
    m_dragStart = event->pos();
    if(const int marker = markerAt(event->pos()); marker >= 0) {
        m_draggedMarker = marker;
        return;
    }
    if(m_mode == Mode::Pick) {
        emit picked(pixelToData(event->pos()).x());
        return;
    }
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
    if(m_draggedMarker >= 0) {
        m_markers[m_draggedMarker].x = std::clamp(pixelToData(event->pos()).x(), m_viewX.min, m_viewX.max);
        update();
    } else if(!(event->buttons() & Qt::LeftButton)) {
        updateIdleCursor(event->pos());
    }
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
    if(m_draggedMarker >= 0) {
        const int marker = std::exchange(m_draggedMarker, -1);
        emit markerMoved(marker, m_markers[marker].x);
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
    const int bin = binAt(pixelToData(pos).x());
    if(bin < 0) {
        emit cursorInfoChanged({});
        return;
    }
    const auto index = static_cast<size_t>(bin);
    QStringList values;
    for(const Curve &curve : m_curves) {
        const double value = index < curve.values.size() ? curve.values[index] : 0;
        values.append(QString("%1: %2").arg(curve.name).arg(m_yAxis.decimals < 0 ? QString::number(value, 'g', 6)
                                                                                 : QString::number(value, 'f', m_yAxis.decimals)));
    }
    const int decimals = m_xAxis.decimals;
    emit cursorInfoChanged(QString("%1 %2–%3 %4   %5")
                                   .arg(m_xAxis.title.section(' ', 0, 0))
                                   .arg(binStart(index), 0, 'f', decimals).arg(binStart(index + 1), 0, 'f', decimals)
                                   .arg(m_xAxis.unit, values.join(QStringLiteral(", "))));
}
