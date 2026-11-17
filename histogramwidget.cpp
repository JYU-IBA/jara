#include "histogramwidget.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QMouseEvent>
#include <QPainter>
#include <QRubberBand>

namespace {
constexpr int MarginLeft = 70;
constexpr int MarginRight = 20;
constexpr int MarginTop = 30;
constexpr int MarginBottom = 50;

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
}

HistogramWidget::HistogramWidget(QWidget *parent) : QWidget(parent) {
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    setMouseTracking(true);
    setMinimumSize(300, 200);
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

void HistogramWidget::resetZoom() {
    m_viewX = m_fullX;
    m_viewY = m_fullY;
    rebin();
    update();
}

QRect HistogramWidget::plotRect() const {
    return rect().adjusted(MarginLeft, MarginTop, -MarginRight, -MarginBottom);
}

double HistogramWidget::xToPixel(double x) const {
    const QRect plot = plotRect();
    return plot.left() + (x - m_viewX.min) / m_viewX.size() * plot.width();
}

double HistogramWidget::yToPixel(double y) const {
    const QRect plot = plotRect();
    return plot.top() + plot.height() - (y - m_viewY.min) / m_viewY.size() * plot.height();
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
    if(!m_image.isNull()) {
        // The bins cover slightly more than the view when the view is not a multiple of the bin width
        const QRectF target(QPointF(xToPixel(m_viewX.min), yToPixel(m_viewY.min + m_binsY * m_binWidthY)),
                            QPointF(xToPixel(m_viewX.min + m_binsX * m_binWidthX), yToPixel(m_viewY.min)));
        painter.save();
        painter.setClipRect(plot);
        painter.drawImage(target, m_image);
        painter.restore();
    }
    drawAxes(painter, plot);
}

void HistogramWidget::drawAxes(QPainter &painter, const QRect &plot) const {
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawRect(plot.adjusted(0, 0, -1, -1));
    const QFontMetrics metrics = painter.fontMetrics();

    const double stepX = niceStep(m_viewX.size(), std::max(2, plot.width() / 80));
    for(double x = std::ceil(m_viewX.min / stepX) * stepX; x <= m_viewX.max; x += stepX) {
        const int px = static_cast<int>(std::round(xToPixel(x)));
        painter.drawLine(px, plot.bottom(), px, plot.bottom() + 5);
        const QString label = QString::number(x);
        painter.drawText(px - metrics.horizontalAdvance(label) / 2, plot.bottom() + 6 + metrics.ascent(), label);
    }
    const double stepY = niceStep(m_viewY.size(), std::max(2, plot.height() / 50));
    for(double y = std::ceil(m_viewY.min / stepY) * stepY; y <= m_viewY.max; y += stepY) {
        const int py = static_cast<int>(std::round(yToPixel(y)));
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
    if(event->button() == Qt::LeftButton && plotRect().contains(event->pos())) {
        m_dragStart = event->pos();
        m_rubberBand->setGeometry(QRect(m_dragStart, QSize()));
        m_rubberBand->show();
    }
}

void HistogramWidget::mouseMoveEvent(QMouseEvent *event) {
    const QRect plot = plotRect();
    if(m_rubberBand->isVisible()) {
        m_rubberBand->setGeometry(QRect(m_dragStart, event->pos()).normalized() & plot);
    }
    if(!plot.contains(event->pos()) || m_counts.empty()) {
        emit cursorInfoChanged({});
        return;
    }
    const QPointF data = pixelToData(event->position());
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
    if(event->button() != Qt::LeftButton || !m_rubberBand->isVisible()) {
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
    m_viewX = {std::floor(topLeft.x()), std::max(std::ceil(bottomRight.x()), std::floor(topLeft.x()) + 2)};
    m_viewY = {std::floor(bottomRight.y()), std::max(std::ceil(topLeft.y()), std::floor(bottomRight.y()) + 2)};
    rebin();
    update();
}

void HistogramWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if(event->button() == Qt::LeftButton) {
        resetZoom();
    }
}
