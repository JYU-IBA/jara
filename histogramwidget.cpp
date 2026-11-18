#include "histogramwidget.h"
#include "elements.h"
#include "project.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRubberBand>

namespace {
constexpr int MarginLeft = 70;
constexpr int MarginRight = 20;
constexpr int MarginTop = 30;
constexpr int MarginBottom = 50;
constexpr double CloseDistance = 8; // Pixels, clicking this close to the first vertex completes a polygon
constexpr double MinimumViewSize = 2; // Channels

// Viridis-like color map, built once
const std::array<QRgb, 256> &colorMap() {
    static const std::array<QRgb, 256> map = []() {
        const std::array<QColor, 5> stops{QColor(0x44, 0x01, 0x54), QColor(0x3b, 0x52, 0x8b), QColor(0x21, 0x91, 0x8c),
                                          QColor(0x5e, 0xc9, 0x62), QColor(0xfd, 0xe7, 0x25)};
        std::array<QRgb, 256> result{};
        for(size_t i = 0; i < result.size(); i++) {
            const double t = static_cast<double>(i) / (result.size() - 1) * (stops.size() - 1);
            const size_t stop = std::min(static_cast<size_t>(t), stops.size() - 2);
            const double f = t - static_cast<double>(stop);
            const QColor &a = stops[stop];
            const QColor &b = stops[stop + 1];
            result[i] = qRgb(static_cast<int>(a.red() + f * (b.red() - a.red())),
                             static_cast<int>(a.green() + f * (b.green() - a.green())),
                             static_cast<int>(a.blue() + f * (b.blue() - a.blue())));
        }
        return result;
    }();
    return map;
}

double niceStep(double range, int targetTicks) {
    const double raw = range / targetTicks;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double r = raw / magnitude;
    const double nice = r < 1.5 ? 1 : r < 3 ? 2 : r < 7 ? 5 : 10;
    return nice * magnitude;
}

double distance(const QPointF &a, const QPointF &b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

// Closest point to p on the segment from a to b
QPointF closestPointOnSegment(const QPointF &p, const QPointF &a, const QPointF &b) {
    const QPointF ab = b - a;
    const double lengthSquared = QPointF::dotProduct(ab, ab);
    if(lengthSquared == 0) {
        return a;
    }
    const double t = std::clamp(QPointF::dotProduct(p - a, ab) / lengthSquared, 0.0, 1.0);
    return a + t * ab;
}

}

HistogramWidget::HistogramWidget(QWidget *parent) : QWidget(parent) {
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(300, 200);
    setCursor(Qt::CrossCursor);
}

void HistogramWidget::setEvents(const std::vector<ToFEEvent> *events) {
    m_events = events;
    m_fullX = {0, 1};
    m_fullY = {0, 1};
    if(events && !events->empty()) {
        const auto [minTof, maxTof] = std::minmax_element(events->begin(), events->end(),
                                                          [](const ToFEEvent &a, const ToFEEvent &b) { return a.tof < b.tof; });
        const auto [minE, maxE] = std::minmax_element(events->begin(), events->end(),
                                                      [](const ToFEEvent &a, const ToFEEvent &b) { return a.energy < b.energy; });
        m_fullX = {std::min(0.0, std::floor(minTof->tof)), std::floor(maxTof->tof) + 1};
        m_fullY = {std::min(0.0, std::floor(minE->energy)), std::floor(maxE->energy) + 1};
    }
    resetZoom();
}

void HistogramWidget::setCuts(QList<Cut> *cuts) {
    m_cuts = cuts;
    update();
}

void HistogramWidget::setHighlightedCut(int index) {
    m_highlightedCut = index;
    update();
}

void HistogramWidget::setSelectedCuts(const QList<int> &indices) {
    m_selectedCuts = indices;
    update();
}

void HistogramWidget::setMode(HistogramWidget::Mode mode) {
    m_mode = mode;
    m_polygon.clear();
    m_rubberBand->hide();
    m_moving = false;
    m_dragVertex = -1;
    m_draggingCut = false;
    setCursor(mode == Mode::Move ? Qt::OpenHandCursor : mode == Mode::Edit ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void HistogramWidget::resetZoom() {
    setView(m_fullX, m_fullY);
}

void HistogramWidget::zoomIn() {
    zoomAt({(m_viewX.min + m_viewX.max) / 2, (m_viewY.min + m_viewY.max) / 2}, 0.5);
}

void HistogramWidget::zoomOut() {
    zoomAt({(m_viewX.min + m_viewX.max) / 2, (m_viewY.min + m_viewY.max) / 2}, 2.0);
}

void HistogramWidget::zoomAt(const QPointF &anchor, double factor) {
    // View edges stay on whole channels, and the view never gets larger than the full range
    auto zoomed = [factor](const Range &view, const Range &full, double anchor) -> Range {
        double size = std::round(view.size() * factor);
        // Small steps must still change the view, otherwise repeated small steps would never zoom
        if(factor < 1) {
            size = std::min(size, view.size() - 1);
        } else if(factor > 1) {
            size = std::max(size, view.size() + 1);
        }
        size = std::max(MinimumViewSize, size);
        if(size >= full.size()) {
            return full;
        }
        const double min = std::round(anchor - (anchor - view.min) * size / view.size());
        return {min, min + size};
    };
    setView(zoomed(m_viewX, m_fullX, anchor.x()), zoomed(m_viewY, m_fullY, anchor.y()));
}

void HistogramWidget::moveByPixels(const QPointF &delta) {
    // The content follows the fingers
    const QRect plot = plotRect();
    m_moveRemainder += QPointF(-delta.x() * m_viewX.size() / plot.width(), delta.y() * m_viewY.size() / plot.height());
    const double dx = std::trunc(m_moveRemainder.x());
    const double dy = std::trunc(m_moveRemainder.y());
    if(dx == 0 && dy == 0) {
        return;
    }
    m_moveRemainder -= QPointF(dx, dy);
    setView({m_viewX.min + dx, m_viewX.max + dx}, {m_viewY.min + dy, m_viewY.max + dy});
}

void HistogramWidget::wheelEvent(QWheelEvent *event) {
    // Trackpads report scroll phases, mouse wheels do not
    const bool trackpad = event->phase() != Qt::NoScrollPhase
                          || (event->pointingDevice() && event->pointingDevice()->type() == QInputDevice::DeviceType::TouchPad);
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

bool HistogramWidget::event(QEvent *event) {
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

void HistogramWidget::setView(const Range &x, const Range &y) {
    // Never show areas outside the full range: shrink to it, then shift back inside
    auto clamped = [](Range view, const Range &full) -> Range {
        const double size = std::min(view.size(), full.size());
        const double min = std::clamp(view.min, full.min, full.max - size);
        return {min, min + size};
    };
    m_viewX = clamped(x, m_fullX);
    m_viewY = clamped(y, m_fullY);
    rebin();
    update();
}

QRect HistogramWidget::plotRect() const {
    return rect().adjusted(MarginLeft, MarginTop, -MarginRight, -MarginBottom);
}

QPointF HistogramWidget::dataToPixel(const QPointF &data) const {
    const QRect plot = plotRect();
    return {plot.left() + (data.x() - m_viewX.min) / m_viewX.size() * plot.width(),
            plot.top() + plot.height() - (data.y() - m_viewY.min) / m_viewY.size() * plot.height()};
}

QPointF HistogramWidget::pixelToData(const QPointF &pixel) const {
    const QRect plot = plotRect();
    return {m_viewX.min + (pixel.x() - plot.left()) / plot.width() * m_viewX.size(),
            m_viewY.min + (plot.top() + plot.height() - pixel.y()) / plot.height() * m_viewY.size()};
}

void HistogramWidget::rebin() {
    const QRect plot = plotRect();
    m_image = QImage();
    m_counts.clear();
    m_maxCount = 0;
    if(!m_events || plot.width() <= 0 || plot.height() <= 0) {
        return;
    }
    // At most about one bin per pixel
    m_binWidthX = std::max(1.0, std::ceil(m_viewX.size() / plot.width()));
    m_binWidthY = std::max(1.0, std::ceil(m_viewY.size() / plot.height()));
    m_binsX = std::max(1, static_cast<int>(std::ceil(m_viewX.size() / m_binWidthX)));
    m_binsY = std::max(1, static_cast<int>(std::ceil(m_viewY.size() / m_binWidthY)));
    m_counts.assign(static_cast<size_t>(m_binsX) * m_binsY, 0);
    for(const auto &event : *m_events) {
        const double bx = std::floor((event.tof - m_viewX.min) / m_binWidthX);
        const double by = std::floor((event.energy - m_viewY.min) / m_binWidthY);
        if(bx < 0 || by < 0 || bx >= m_binsX || by >= m_binsY) {
            continue;
        }
        quint32 &count = m_counts[static_cast<size_t>(by) * m_binsX + static_cast<size_t>(bx)];
        count++;
        m_maxCount = std::max(m_maxCount, count);
    }

    m_image = QImage(m_binsX, m_binsY, QImage::Format_RGB32);
    m_image.fill(Qt::white);
    const double logMax = std::log(static_cast<double>(std::max<quint32>(m_maxCount, 2)));
    const auto &colors = colorMap();
    for(int by = 0; by < m_binsY; by++) {
        auto *line = reinterpret_cast<QRgb *>(m_image.scanLine(m_binsY - 1 - by)); // E increases upwards
        for(int bx = 0; bx < m_binsX; bx++) {
            const quint32 count = m_counts[static_cast<size_t>(by) * m_binsX + bx];
            if(count > 0) {
                const double t = std::log(static_cast<double>(count)) / logMax;
                line[bx] = colors[std::clamp(static_cast<int>(t * 255), 0, 255)];
            }
        }
    }
}

void HistogramWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    const QRect plot = plotRect();
    if(plot.width() <= 0 || plot.height() <= 0) {
        return;
    }
    painter.fillRect(plot, Qt::white);
    painter.save();
    painter.setClipRect(plot);
    if(!m_image.isNull()) {
        // The bins cover slightly more than the view when the view is not a multiple of the bin width
        const QRectF target(dataToPixel({m_viewX.min, m_viewY.min + m_binsY * m_binWidthY}),
                            dataToPixel({m_viewX.min + m_binsX * m_binWidthX, m_viewY.min}));
        painter.drawImage(target, m_image);
    }
    painter.setRenderHint(QPainter::Antialiasing);
    drawCuts(painter);
    drawPolygonInProgress(painter);
    painter.restore();
    drawAxes(painter, plot);
}

QPolygonF HistogramWidget::cutInPixels(int index) const {
    QPolygonF polygon;
    for(const QPointF &point : m_cuts->at(index).polygon) {
        polygon.append(dataToPixel(point));
    }
    return polygon;
}

void HistogramWidget::drawCuts(QPainter &painter) const {
    if(!m_cuts) {
        return;
    }
    for(int i = 0; i < m_cuts->size(); i++) {
        const QPolygonF polygon = cutInPixels(i);
        const bool highlighted = i == m_highlightedCut || m_selectedCuts.contains(i);
        // A light outline under the line keeps it visible on both dark and light parts of the histogram
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 200), highlighted ? 5 : 3.5));
        painter.drawPolygon(polygon);
        const QColor &color = m_cuts->at(i).color;
        painter.setPen(QPen(color, highlighted ? 2.5 : 1.5));
        painter.drawPolygon(polygon);
        if(i == m_highlightedCut && m_mode == Mode::Edit) {
            painter.setPen(color);
            painter.setBrush(Qt::white);
            for(const QPointF &vertex : polygon) {
                painter.drawRect(QRectF(vertex - QPointF(4, 4), QSizeF(8, 8)));
            }
        }
        drawCutLabel(painter, m_cuts->at(i), polygon);
    }
}

void HistogramWidget::drawCutLabel(QPainter &painter, const Cut &cut, const QPolygonF &polygon) const {
    if(polygon.isEmpty()) {
        return;
    }
    // Just above and right of the topmost vertex, mass number as a superscript
    const QPointF anchor = *std::min_element(polygon.begin(), polygon.end(),
                                             [](const QPointF &a, const QPointF &b) { return a.y() < b.y(); });
    const QString symbol = Elements::symbol(cut.z);
    const QString mass = cut.a > 0 ? QString::number(cut.a) : QString();
    const QFont symbolFont = painter.font();
    QFont massFont = symbolFont;
    if(symbolFont.pointSizeF() > 0) {
        massFont.setPointSizeF(symbolFont.pointSizeF() * 0.7);
    } else {
        massFont.setPixelSize(std::max(1, static_cast<int>(symbolFont.pixelSize() * 0.7)));
    }
    const QFontMetricsF symbolMetrics(symbolFont);
    const QFontMetricsF massMetrics(massFont);
    const double raise = symbolMetrics.ascent() * 0.4;
    const double massWidth = mass.isEmpty() ? 0 : massMetrics.horizontalAdvance(mass);
    const double ascent = std::max(symbolMetrics.ascent(), mass.isEmpty() ? 0 : raise + massMetrics.ascent());
    const double baseline = anchor.y() - 5 - symbolMetrics.descent();
    const QRectF box(QPointF(anchor.x() + 5, baseline - ascent - 1),
                     QPointF(anchor.x() + 5 + massWidth + symbolMetrics.horizontalAdvance(symbol) + 4, baseline + symbolMetrics.descent() + 1));

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 200));
    painter.drawRoundedRect(box, 3, 3);
    painter.setPen(cut.color);
    if(!mass.isEmpty()) {
        painter.setFont(massFont);
        painter.drawText(QPointF(box.left() + 2, baseline - raise), mass);
    }
    painter.setFont(symbolFont);
    painter.drawText(QPointF(box.left() + 2 + massWidth, baseline), symbol);
    painter.restore();
}

void HistogramWidget::drawPolygonInProgress(QPainter &painter) const {
    if(m_polygon.isEmpty()) {
        return;
    }
    QPolygonF polyline;
    for(const QPointF &point : m_polygon) {
        polyline.append(dataToPixel(point));
    }
    const bool closing = m_polygon.size() >= 3 && distance(polyline.first(), m_cursor) < CloseDistance;
    polyline.append(closing ? polyline.first() : QPointF(m_cursor));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, 200), 3.5));
    painter.drawPolyline(polyline);
    painter.setPen(QPen(Qt::black, 1.5, Qt::DashLine));
    painter.drawPolyline(polyline);
    painter.setPen(Qt::black);
    painter.setBrush(Qt::white);
    for(int i = 0; i < m_polygon.size(); i++) {
        const double size = i == 0 && closing ? 10 : 6; // Show that clicking the first vertex completes the polygon
        painter.drawRect(QRectF(0, 0, size, size).translated(polyline.at(i) - QPointF(size / 2, size / 2)));
    }
}

void HistogramWidget::drawAxes(QPainter &painter, const QRect &plot) const {
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawRect(plot.adjusted(0, 0, -1, -1));
    const QFontMetrics metrics = painter.fontMetrics();

    const double stepX = niceStep(m_viewX.size(), std::max(2, plot.width() / 80));
    for(double x = std::ceil(m_viewX.min / stepX) * stepX; x <= m_viewX.max; x += stepX) {
        const int px = static_cast<int>(std::round(dataToPixel({x, 0}).x()));
        painter.drawLine(px, plot.bottom(), px, plot.bottom() + 5);
        const QString label = QString::number(x);
        painter.drawText(px - metrics.horizontalAdvance(label) / 2, plot.bottom() + 6 + metrics.ascent(), label);
    }
    const double stepY = niceStep(m_viewY.size(), std::max(2, plot.height() / 50));
    for(double y = std::ceil(m_viewY.min / stepY) * stepY; y <= m_viewY.max; y += stepY) {
        const int py = static_cast<int>(std::round(dataToPixel({0, y}).y()));
        painter.drawLine(plot.left() - 5, py, plot.left(), py);
        const QString label = QString::number(y);
        painter.drawText(plot.left() - 8 - metrics.horizontalAdvance(label), py + metrics.ascent() / 2, label);
    }

    const QString xTitle = QStringLiteral("ToF (ch)");
    painter.drawText(plot.center().x() - metrics.horizontalAdvance(xTitle) / 2, height() - 8, xTitle);
    const QString yTitle = QStringLiteral("E (ch)");
    painter.save();
    painter.translate(14, plot.center().y() + metrics.horizontalAdvance(yTitle) / 2);
    painter.rotate(-90);
    painter.drawText(0, 0, yTitle);
    painter.restore();

    if(m_events) {
        painter.drawText(plot.left(), MarginTop - 10,
                         QString("Bin %1 × %2 ch, max %3 counts/bin, log color scale")
                                 .arg(m_binWidthX).arg(m_binWidthY).arg(m_maxCount));
    }
}

void HistogramWidget::resizeEvent(QResizeEvent *) {
    rebin();
}

void HistogramWidget::mousePressEvent(QMouseEvent *event) {
    if(event->button() == Qt::RightButton && m_mode == Mode::Edit) {
        // Remove a vertex, a polygon needs at least three
        const int vertex = vertexAt(event->pos());
        if(vertex >= 0 && (*m_cuts)[m_highlightedCut].polygon.size() > 3) {
            (*m_cuts)[m_highlightedCut].polygon.removeAt(vertex);
            update();
            emit cutEdited(m_highlightedCut);
        }
        return;
    }
    if(event->button() != Qt::LeftButton || !plotRect().contains(event->pos())) {
        return;
    }
    switch(m_mode) {
        case Mode::Zoom:
            m_dragStart = event->pos();
            m_rubberBand->setGeometry(QRect(m_dragStart, QSize()));
            m_rubberBand->show();
            break;
        case Mode::Move:
            m_dragStart = event->pos();
            m_moveStartX = m_viewX;
            m_moveStartY = m_viewY;
            m_moving = true;
            setCursor(Qt::ClosedHandCursor);
            break;
        case Mode::Select:
            if(m_polygon.size() >= 3 && distance(dataToPixel(m_polygon.first()), event->pos()) < CloseDistance) {
                completePolygon();
            } else {
                m_polygon.append(pixelToData(event->position()));
                update();
            }
            break;
        case Mode::Edit: {
            m_cutChanged = false;
            m_dragVertex = vertexAt(event->pos());
            if(m_dragVertex >= 0) {
                break;
            }
            const int cut = cutAt(event->pos());
            if(cut < 0) {
                break;
            }
            if(cut != m_highlightedCut) {
                m_highlightedCut = cut;
                emit cutClicked(cut);
                update();
            }
            m_draggingCut = true;
            m_dragStartData = pixelToData(event->position());
            m_cutAtDragStart = m_cuts->at(cut).polygon;
            setCursor(Qt::ClosedHandCursor);
            break;
        }
    }
}

void HistogramWidget::mouseMoveEvent(QMouseEvent *event) {
    m_cursor = event->pos();
    if(m_rubberBand->isVisible()) {
        m_rubberBand->setGeometry(QRect(m_dragStart, event->pos()).normalized() & plotRect());
    }
    if(m_moving) {
        // Move by whole channels
        const QRect plot = plotRect();
        const double dx = std::round((m_dragStart.x() - event->pos().x()) * m_moveStartX.size() / plot.width());
        const double dy = std::round((event->pos().y() - m_dragStart.y()) * m_moveStartY.size() / plot.height());
        setView({m_moveStartX.min + dx, m_moveStartX.max + dx}, {m_moveStartY.min + dy, m_moveStartY.max + dy});
    }
    if(m_dragVertex >= 0) {
        (*m_cuts)[m_highlightedCut].polygon[m_dragVertex] = pixelToData(event->position());
        m_cutChanged = true;
        update();
    } else if(m_draggingCut) {
        const QPointF delta = pixelToData(event->position()) - m_dragStartData;
        QList<QPointF> &polygon = (*m_cuts)[m_highlightedCut].polygon;
        for(int i = 0; i < polygon.size(); i++) {
            polygon[i] = m_cutAtDragStart.at(i) + delta;
        }
        m_cutChanged = true;
        update();
    } else if(m_mode == Mode::Edit) {
        updateEditCursor(event->pos());
    }
    if(!m_polygon.isEmpty()) {
        update();
    }
    updateCursorInfo(event->pos());
}

void HistogramWidget::updateEditCursor(const QPoint &pos) {
    if(vertexAt(pos) >= 0) {
        setCursor(Qt::SizeAllCursor);
    } else if(cutAt(pos) >= 0) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

int HistogramWidget::cutAt(const QPoint &pos) const {
    if(!m_cuts) {
        return -1;
    }
    for(int i = static_cast<int>(m_cuts->size()) - 1; i >= 0; i--) {
        if(cutInPixels(i).containsPoint(pos, Qt::OddEvenFill)) {
            return i;
        }
    }
    return -1;
}

int HistogramWidget::vertexAt(const QPoint &pos) const {
    if(!m_cuts || m_highlightedCut < 0 || m_highlightedCut >= m_cuts->size()) {
        return -1;
    }
    const QPolygonF polygon = cutInPixels(m_highlightedCut);
    for(int i = 0; i < polygon.size(); i++) {
        if(distance(polygon.at(i), pos) < CloseDistance) {
            return i;
        }
    }
    return -1;
}

bool HistogramWidget::insertVertexAt(const QPoint &pos) {
    if(!m_cuts || m_highlightedCut < 0 || m_highlightedCut >= m_cuts->size()) {
        return false;
    }
    const QPolygonF polygon = cutInPixels(m_highlightedCut);
    for(int i = 0; i < polygon.size(); i++) {
        const QPointF closest = closestPointOnSegment(pos, polygon.at(i), polygon.at((i + 1) % polygon.size()));
        if(distance(closest, pos) < CloseDistance) {
            (*m_cuts)[m_highlightedCut].polygon.insert(i + 1, pixelToData(closest));
            return true;
        }
    }
    return false;
}

void HistogramWidget::updateCursorInfo(const QPoint &pos) {
    if(!plotRect().contains(pos) || m_counts.empty()) {
        emit cursorInfoChanged({});
        return;
    }
    const QPointF data = pixelToData(pos);
    const int bx = static_cast<int>(std::floor((data.x() - m_viewX.min) / m_binWidthX));
    const int by = static_cast<int>(std::floor((data.y() - m_viewY.min) / m_binWidthY));
    if(bx < 0 || by < 0 || bx >= m_binsX || by >= m_binsY) {
        emit cursorInfoChanged({});
        return;
    }
    const double tof = m_viewX.min + bx * m_binWidthX;
    const double energy = m_viewY.min + by * m_binWidthY;
    const QString tofText = m_binWidthX > 1 ? QString("%1–%2").arg(tof).arg(tof + m_binWidthX - 1) : QString::number(tof);
    const QString energyText = m_binWidthY > 1 ? QString("%1–%2").arg(energy).arg(energy + m_binWidthY - 1) : QString::number(energy);
    emit cursorInfoChanged(QString("ToF %1 ch, E %2 ch: %3 counts")
                                   .arg(tofText, energyText)
                                   .arg(m_counts[static_cast<size_t>(by) * m_binsX + bx]));
}

void HistogramWidget::mouseReleaseEvent(QMouseEvent *event) {
    if(event->button() != Qt::LeftButton) {
        return;
    }
    if(m_moving) {
        m_moving = false;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if(m_dragVertex >= 0 || m_draggingCut) {
        m_dragVertex = -1;
        m_draggingCut = false;
        updateEditCursor(event->pos());
        if(m_cutChanged) {
            emit cutEdited(m_highlightedCut);
        }
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
    // Zoom to whole channels
    const QPointF topLeft = pixelToData(selection.topLeft());
    const QPointF bottomRight = pixelToData(selection.bottomRight());
    const double minX = std::floor(topLeft.x());
    const double minY = std::floor(bottomRight.y());
    setView({minX, std::max(std::ceil(bottomRight.x()), minX + MinimumViewSize)},
            {minY, std::max(std::ceil(topLeft.y()), minY + MinimumViewSize)});
}

void HistogramWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if(event->button() != Qt::LeftButton) {
        return;
    }
    if(m_mode == Mode::Select) {
        // The first click of the double-click already added the last vertex
        if(m_polygon.size() >= 3) {
            completePolygon();
        }
    } else if(m_mode == Mode::Zoom) {
        resetZoom();
    } else if(m_mode == Mode::Edit && insertVertexAt(event->pos())) {
        update();
        emit cutEdited(m_highlightedCut);
    }
}

void HistogramWidget::keyPressEvent(QKeyEvent *event) {
    if(m_mode == Mode::Select && !m_polygon.isEmpty()) {
        if(event->key() == Qt::Key_Escape) {
            m_polygon.clear();
            update();
            return;
        }
        if(event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
            m_polygon.removeLast();
            update();
            return;
        }
        if(event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if(m_polygon.size() >= 3) {
                completePolygon();
            }
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void HistogramWidget::completePolygon() {
    const QList<QPointF> polygon = m_polygon;
    m_polygon.clear();
    update();
    emit polygonCompleted(polygon);
}
