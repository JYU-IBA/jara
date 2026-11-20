#ifndef JARA_SPECTRUMPLOTWIDGET_H
#define JARA_SPECTRUMPLOTWIDGET_H

#include <optional>
#include <vector>

#include <QAction>
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
        QString valueSuffix; // After each value in the cursor info, as "e15"
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
        // The full range of a linear axis starts from zero, otherwise it covers the values with a margin, so that
        // small differences between them can be seen
        bool includeZero = true;
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

    // Text in the plot, centred at x and as near to y as the other labels allow: labels are moved apart vertically so
    // that they do not overlap, see placeLabels(). A label with a position is centred there instead. Labels can be
    // dragged, which emits labelMoved() with the new position.
    struct Label {
        double x; // In x axis units
        double y; // As the values of the curves
        QString text;
        QColor color;
        std::optional<QPointF> position; // Of the centre, x in axis units and y as the values of the curves
    };

    struct Curve {
        QString name; // In the legend and the cursor info
        QColor color;
        size_t events; // Shown in the legend
        std::vector<double> values; // Of the bins
        Qt::PenStyle style = Qt::SolidLine;
        QString legendSuffix; // After the name in the legend only
        std::vector<double> errors; // Of the bins, drawn as error bars at their centres when given
        QList<QPointF> line; // Drawn as a thin line through the points, e.g. a fit, x and y as the values
    };

    // What happens to the view when the curves are replaced
    enum class ViewChange {
        ShowAll,
        KeepEnergyRange, // The x axis is kept, the y axis is scaled to show all values
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
    void setLegendCorner(Qt::Corner corner);
    void setContextActions(const QList<QAction *> &actions); // Added to the end of the context menu, not owned
    void setOverlay(const QList<QPointF> &points, const QColor &color); // Line through the points, x and counts
    void setMarkers(const QList<Marker> &markers);
    void setLabels(const QList<Label> &labels);
    void setXView(double min, double max); // The counts axis is kept
    // The shown ranges, y as the values of the curves (not log10 on a log scale)
    void viewRange(double &xMin, double &xMax, double &yMin, double &yMax) const;
    void showRange(double xMin, double xMax, double yMin, double yMax); // Kept within the data, as zooming is
    // Exactly these ranges, also beyond the data, e.g. for printing. Until the curves or the axes change.
    void setFixedRange(double xMin, double xMax, double yMin, double yMax);
    void setStale(bool stale); // Drawn faded and dashed
    // The y axis always shows the values of the bins in view, zooming and moving change only the x axis
    void setAutoYScale(bool autoYScale);
    bool logScale() const { return m_logScale; }

public slots:
    void setLogScale(bool logScale);
    void setMode(SpectrumPlotWidget::Mode mode);
    void zoomIn();
    void zoomOut();
    void resetZoom();

signals:
    void cursorInfoChanged(const QString &text);
    void cursorMoved(double x); // Inside the bins, just before cursorInfoChanged()
    void logScaleChanged(bool logScale);
    void viewChangedByUser(); // Zoomed, moved or the scale changed by the user
    void picked(double x);
    void markerMoved(int index, double x);
    void legendCornerChanged(Qt::Corner corner); // The legend was dragged there
    void labelMoved(int index, QPointF position); // Of its centre, x in axis units and y as the values of the curves

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
    double fromPlotY(double y) const; // The inverse of toPlotY()
    QPointF dataToPixel(double x, double y) const; // y as from toPlotY()
    QPointF pixelToData(const QPointF &pixel) const;
    void updateFullRange();
    Range autoYRange(const Range &x) const; // Covers the values of the bins in the x range
    void setView(const Range &x, const Range &y);
    void zoomAt(const QPointF &anchor, double factor);
    void moveByPixels(const QPointF &delta);
    void updateCursorInfo(const QPoint &pos);
    int markerAt(const QPoint &pos) const; // Index of the draggable marker near the position, -1 when there is none
    QList<QRectF> placeLabels() const; // Rectangles of the labels in pixels
    int labelAt(const QPoint &pos) const; // Index of the topmost label at the position, -1 when there is none
    void drawLabels(QPainter &painter) const;
    void updateIdleCursor(const QPoint &pos);
    double binStart(size_t bin) const;
    int binAt(double x) const; // -1 when outside the bins
    void drawCurve(QPainter &painter, const Curve &curve) const;
    void drawRegions(QPainter &painter, const QRect &plot) const;
    void drawOverlay(QPainter &painter) const;
    void drawMarkers(QPainter &painter, const QRect &plot) const;
    void drawAxes(QPainter &painter, const QRect &plot) const;
    QRect legendRect() const; // Empty when there is no legend, follows the mouse while it is dragged
    void drawLegend(QPainter &painter) const;

    XAxis m_xAxis;
    YAxis m_yAxis;
    std::vector<double> m_edges;
    QList<Region> m_regions;
    QString m_infoText;
    Qt::Corner m_legendCorner = Qt::TopRightCorner;
    bool m_draggingLegend = false;
    QPoint m_legendGrab; // From the top left corner of the dragged legend to the mouse
    QPoint m_legendDragPos; // Of the mouse
    QList<QAction *> m_contextActions;
    QList<Curve> m_curves;
    QList<QPointF> m_overlay;
    QColor m_overlayColor;
    QList<Marker> m_markers;
    int m_draggedMarker = -1;
    QList<Label> m_labels;
    int m_draggedLabel = -1;
    QPointF m_labelGrab; // From the centre of the dragged label to the mouse, pixels
    QPointF m_labelDragCentre; // Of the dragged label, pixels
    double m_binWidth = 1; // In x axis units
    size_t m_bins = 0;
    double m_maxValue = 0;
    double m_minValue = 0; // Lowest value minus its error
    bool m_logScale = false; // Spectra start with a linear count axis
    bool m_stale = false;
    bool m_autoYScale = false;
    Mode m_mode = Mode::Zoom;
    Range m_fullX{0, 1}, m_fullY{0, 1};
    Range m_viewX{0, 1}, m_viewY{0, 1};
    QRubberBand *m_rubberBand;
    QPoint m_dragStart;
    bool m_moving = false;
    Range m_moveStartX{0, 1}, m_moveStartY{0, 1};
};

#endif // JARA_SPECTRUMPLOTWIDGET_H
