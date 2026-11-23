/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_HISTOGRAMWIDGET_H
#define JARA_HISTOGRAMWIDGET_H

#include <vector>

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QWidget>

#include "defaults.h"
#include "tofedata.h"

struct Cut;
class QRubberBand;

// 2D ToF-E histogram with a logarithmic or linear color scale. Supports box zoom, moving the view, drawing polygon
// selections and editing existing cuts, which are drawn on top of the histogram. The mouse wheel and trackpad
// pinch zoom around the cursor, two-finger trackpad scrolling moves the view.
class HistogramWidget : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Zoom, Move, Select, Edit };

    // Line through points in data coordinates drawn on top of the histogram, e.g. a fit
    struct OverlayLine {
        QPolygonF points;
        QColor color;
        Qt::PenStyle style = Qt::SolidLine;
        double width = 1.5; // Pixels
    };

    explicit HistogramWidget(QWidget *parent = nullptr);
    void setEvents(const std::vector<ToFEEvent> *events); // Not owned, must outlive the widget
    void setCuts(QList<Cut> *cuts); // Not owned, edited in Edit mode. Call update() after changing them.
    void setHighlightedCut(int index); // The cut edited in Edit mode, -1 for none
    void setSelectedCuts(const QList<int> &indices); // Drawn highlighted
    void applyPreferences(const HistogramPreferences &preferences); // Binning and color scheme, the scale is kept
    void setLogScale(bool logScale);
    // Names and units of the axes, ToF and E in channels by default. Other data is passed in ToFEEvent as x and y.
    void setAxes(const QString &xName, const QString &xUnit, const QString &yName, const QString &yUnit);
    void setFullXRange(double min, double max); // Instead of the range of the events, call after setEvents()
    void setFullYRange(double min, double max); // Same as above
    void setOverlayLines(const QList<OverlayLine> &lines);
    void setVerticalMarkers(const QList<double> &x); // Dashed lines
    void setMarkersDraggable(bool draggable); // Except in Move mode, markerMoved() is emitted when one is released

public slots:
    void setMode(HistogramWidget::Mode mode);
    void zoomIn();
    void zoomOut();
    void resetZoom();

signals:
    void cursorInfoChanged(const QString &text);
    void cursorMoved(QPointF position); // In data coordinates, inside the plot, just before cursorInfoChanged()
    void polygonCompleted(const QList<QPointF> &polygon); // ToF and E in channels
    void cutClicked(int index); // In Edit mode
    void cutEdited(int index);
    void markerMoved(int index, double x);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

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
    void zoomAt(const QPointF &anchor, double factor); // Anchor in channels stays at the same place on screen
    void moveByPixels(const QPointF &delta);
    void rebin();
    void updateCursorInfo(const QPoint &pos);
    void completePolygon();
    QPolygonF cutInPixels(int index) const;
    int cutAt(const QPoint &pos) const; // Topmost cut containing pos, -1 if none
    int vertexAt(const QPoint &pos) const; // Vertex of the highlighted cut at pos, -1 if none
    bool insertVertexAt(const QPoint &pos); // On an edge of the highlighted cut
    void updateEditCursor(const QPoint &pos);
    int markerAt(const QPoint &pos) const; // Index of the draggable marker near the position, -1 when there is none
    void drawAxes(QPainter &painter, const QRect &plot) const;
    void drawCuts(QPainter &painter) const;
    void drawCutLabel(QPainter &painter, const Cut &cut, const QPolygonF &polygon) const;
    void drawPolygonInProgress(QPainter &painter) const;
    void drawMarkers(QPainter &painter) const;
    void drawOverlayLines(QPainter &painter) const;

    const std::vector<ToFEEvent> *m_events = nullptr;
    QList<Cut> *m_cuts = nullptr;
    int m_highlightedCut = -1;
    QList<int> m_selectedCuts;
    QString m_xName = QStringLiteral("ToF");
    QString m_xUnit = QStringLiteral("ch");
    QString m_yName = QStringLiteral("E");
    QString m_yUnit = QStringLiteral("ch");
    QList<double> m_markers;
    QList<OverlayLine> m_overlayLines;
    bool m_markersDraggable = false;
    int m_draggedMarker = -1;
    Mode m_mode = Mode::Zoom;
    HistogramPreferences m_preferences;
    bool m_logScale;
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
    QPointF m_moveRemainder; // Channels not yet moved when scrolling, the view moves by whole channels
    QList<QPointF> m_polygon; // Selection being drawn, in channels
    QPoint m_cursor; // Last mouse position, for the line to the next polygon vertex
    int m_dragVertex = -1; // Vertex of the highlighted cut being dragged in Edit mode
    bool m_draggingCut = false; // The whole highlighted cut is being dragged in Edit mode
    bool m_cutChanged = false;
    QPointF m_dragStartData;
    QList<QPointF> m_cutAtDragStart;
};

#endif // JARA_HISTOGRAMWIDGET_H
