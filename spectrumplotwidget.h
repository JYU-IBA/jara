#ifndef JARA_SPECTRUMPLOTWIDGET_H
#define JARA_SPECTRUMPLOTWIDGET_H

#include <vector>

#include <QColor>
#include <QList>
#include <QPointF>
#include <QString>
#include <QWidget>

class QRubberBand;

// Step between axis ticks for about targetTicks ticks over the range: 1, 2 or 5 times a power of ten
double niceStep(double range, int targetTicks);

// Spectra drawn as steps, with a legend and a logarithmic or linear count axis. Energy spectra by default, the axes
// can be set up for other quantities, e.g. concentrations against depth with bins of different widths. Supports box zoom and moving the view. The mouse wheel and trackpad pinch zoom
// around the cursor, two-finger trackpad scrolling moves the view. An overlay curve and vertical markers can be drawn
// on top, markers can be dragged.
class SpectrumPlotWidget : public QWidget {
    Q_OBJECT

public:
    enum class Mode {
        Zoom,
        Move,
        Pick // A click emits picked()
    };

    // Bin i covers x from origin + i * binWidth to origin + (i + 1) * binWidth
    struct XAxis {
        QString title = QStringLiteral("E (MeV)");
        QString unit = QStringLiteral("MeV"); // In the cursor info
        int decimals = 4; // In the cursor info
        double origin = 0;
        double binWidthScale = 1000; // The bin width is shown multiplied by this, in the unit below
        QString binWidthUnit = QStringLiteral("keV");
    };

    struct YAxis {
        QString title = QStringLiteral("Counts");
        QString maxUnit = QStringLiteral("counts/bin"); // Of the highest value in the info above the plot
        double logFloor = 0.5; // Bottom of the full range on a log scale, zero is drawn below it
        int decimals = -1; // Of the values in the cursor info, -1 for up to six significant digits
        bool legendEvents = true; // The numbers of events of the curves are shown in the legend
    };

    // Shaded background, e.g. an interval of depths
    struct Region {
        double min;
        double max;
        QColor color;
    };

    struct Marker {
        double x;
        QColor color;
        Qt::PenStyle style = Qt::SolidLine;
        bool draggable = false; // Moved by dragging it, markerMoved() is emitted when it is released
    };

    struct Curve {
        QString name; // In the legend and the cursor info
        QColor color;
        size_t events; // Shown in the legend
        std::vector<double> values; // Of the bins
        Qt::PenStyle style = Qt::SolidLine;
    };

    // What happens to the view when the curves are replaced
    enum class ViewChange {
        ShowAll,
        KeepEnergyRange, // The counts axis is scaled to show all counts
        Keep
    };

    explicit SpectrumPlotWidget(QWidget *parent = nullptr);
    void setXAxis(const XAxis &axis); // Before the curves
    void setYAxis(const YAxis &axis);
    // Bin i covers x from edges[i] to edges[i + 1]. Replaces the bins of the x axis, an empty list returns to them.
    // Before the curves.
    void setBinEdges(const std::vector<double> &edges);
    void setCurves(double binWidth, const QList<Curve> &curves, ViewChange viewChange); // Bin width in x axis units
    void setRegions(const QList<Region> &regions);
    void setInfoText(const QString &text); // Above the plot instead of the bin width and the highest value
    void setOverlay(const QList<QPointF> &points, const QColor &color); // Line through the points, x and counts
    void setMarkers(const QList<Marker> &markers);
    void setXView(double min, double max); // The counts axis is kept
    void setStale(bool stale); // Drawn faded and dashed
    bool logScale() const { return m_logScale; }

public slots:
    void setLogScale(bool logScale);
    void setMode(SpectrumPlotWidget::Mode mode);
    void zoomIn();
    void zoomOut();
    void resetZoom();

signals:
    void cursorInfoChanged(const QString &text);
    void logScaleChanged(bool logScale);
    void picked(double x);
    void markerMoved(int index, double x);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    struct Range {
        double min;
        double max;
        double size() const { return max - min; }
    };

    QRect plotRect() const;
    double toPlotY(double count) const; // Count to the y coordinate of the view, log10 of it on a log scale
    QPointF dataToPixel(double x, double y) const; // y as from toPlotY()
    QPointF pixelToData(const QPointF &pixel) const;
    void updateFullRange();
    void setView(const Range &x, const Range &y);
    void zoomAt(const QPointF &anchor, double factor);
    void moveByPixels(const QPointF &delta);
    void updateCursorInfo(const QPoint &pos);
    int markerAt(const QPoint &pos) const; // Index of the draggable marker near the position, -1 when there is none
    void updateIdleCursor(const QPoint &pos);
    double binStart(size_t bin) const;
    int binAt(double x) const; // -1 when outside the bins
    void drawCurve(QPainter &painter, const Curve &curve) const;
    void drawRegions(QPainter &painter, const QRect &plot) const;
    void drawOverlay(QPainter &painter) const;
    void drawMarkers(QPainter &painter, const QRect &plot) const;
    void drawAxes(QPainter &painter, const QRect &plot) const;
    void drawLegend(QPainter &painter, const QRect &plot) const;

    XAxis m_xAxis;
    YAxis m_yAxis;
    std::vector<double> m_edges;
    QList<Region> m_regions;
    QString m_infoText;
    QList<Curve> m_curves;
    QList<QPointF> m_overlay;
    QColor m_overlayColor;
    QList<Marker> m_markers;
    int m_draggedMarker = -1;
    double m_binWidth = 1; // In x axis units
    size_t m_bins = 0;
    double m_maxValue = 0;
    bool m_logScale = false; // Spectra start with a linear count axis
    bool m_stale = false;
    Mode m_mode = Mode::Zoom;
    Range m_fullX{0, 1}, m_fullY{0, 1};
    Range m_viewX{0, 1}, m_viewY{0, 1};
    QRubberBand *m_rubberBand;
    QPoint m_dragStart;
    bool m_moving = false;
    Range m_moveStartX{0, 1}, m_moveStartY{0, 1};
};

#endif // JARA_SPECTRUMPLOTWIDGET_H
