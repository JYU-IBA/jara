#ifndef JARA_HISTOGRAMWIDGET_H
#define JARA_HISTOGRAMWIDGET_H

#include <vector>

#include <QImage>
#include <QWidget>

#include "tofedata.h"

class QRubberBand;

// 2D ToF-E histogram with logarithmic color scale. Drag to zoom, double-click to reset.
class HistogramWidget : public QWidget {
    Q_OBJECT

public:
    explicit HistogramWidget(QWidget *parent = nullptr);
    void setEvents(const std::vector<ToFEEvent> *events); // Not owned, must outlive the widget
    void resetZoom();

signals:
    void cursorInfoChanged(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    struct Range {
        double min;
        double max;
        double size() const { return max - min; }
    };

    QRect plotRect() const;
    double xToPixel(double x) const;
    double yToPixel(double y) const;
    QPointF pixelToData(const QPointF &pixel) const;
    void rebin();
    void drawAxes(QPainter &painter, const QRect &plot) const;

    const std::vector<ToFEEvent> *m_events = nullptr;
    Range m_fullX{0, 1}, m_fullY{0, 1};
    Range m_viewX{0, 1}, m_viewY{0, 1};
    double m_binWidthX = 1; // Channels per bin, integer valued so that channels are not split between bins
    double m_binWidthY = 1;
    int m_binsX = 0;
    int m_binsY = 0;
    std::vector<quint32> m_counts;
    quint32 m_maxCount = 0;
    QImage m_image;
    QRubberBand *m_rubberBand;
    QPoint m_dragStart;
};

#endif // JARA_HISTOGRAMWIDGET_H
