#ifndef JARA_HISTOGRAMWIDGET_H
#define JARA_HISTOGRAMWIDGET_H

#include <vector>

#include <QImage>
#include <QList>
#include <QPointF>
#include <QWidget>

#include "tofedata.h"

struct Cut;
class QRubberBand;

// 2D ToF-E histogram with logarithmic color scale. Supports box zoom, moving the view and drawing polygon
// selections. Existing cuts are drawn on top of the histogram.
class HistogramWidget : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Zoom, Move, Select };

    explicit HistogramWidget(QWidget *parent = nullptr);
    void setEvents(const std::vector<ToFEEvent> *events); // Not owned, must outlive the widget
    void setCuts(const QList<Cut> *cuts); // Not owned, call update() after changing them
    void setHighlightedCut(int index); // -1 for none

public slots:
    void setMode(HistogramWidget::Mode mode);
    void zoomIn();
    void zoomOut();
    void resetZoom();

signals:
    void cursorInfoChanged(const QString &text);
    void polygonCompleted(const QList<QPointF> &polygon); // ToF and E in channels

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Range {
        double min;
        double max;
        double size() const { return max - min; }
    };

    QRect plotRect() const;
    QPointF dataToPixel(const QPointF &data) const;
    QPointF pixelToData(const QPointF &pixel) const;
    void setView(const Range &x, const Range &y);
    void zoom(double factor);
    void rebin();
    void updateCursorInfo(const QPoint &pos);
    void completePolygon();
    void drawAxes(QPainter &painter, const QRect &plot) const;
    void drawCuts(QPainter &painter) const;
    void drawPolygonInProgress(QPainter &painter) const;

    const std::vector<ToFEEvent> *m_events = nullptr;
    const QList<Cut> *m_cuts = nullptr;
    int m_highlightedCut = -1;
    Mode m_mode = Mode::Zoom;
    Range m_fullX{0, 1}, m_fullY{0, 1};
    Range m_viewX{0, 1}, m_viewY{0, 1}; // Edges are whole channels so that channels are not split between bins
    double m_binWidthX = 1; // Channels per bin, integer valued
    double m_binWidthY = 1;
    int m_binsX = 0;
    int m_binsY = 0;
    std::vector<quint32> m_counts;
    quint32 m_maxCount = 0;
    QImage m_image;
    QRubberBand *m_rubberBand;
    QPoint m_dragStart;
    bool m_moving = false;
    Range m_moveStartX{0, 1}, m_moveStartY{0, 1};
    QList<QPointF> m_polygon; // Selection being drawn, in channels
    QPoint m_cursor; // Last mouse position, for the line to the next polygon vertex
};

#endif // JARA_HISTOGRAMWIDGET_H
