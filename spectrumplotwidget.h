#ifndef JARA_SPECTRUMPLOTWIDGET_H
#define JARA_SPECTRUMPLOTWIDGET_H

#include <vector>

#include <QColor>
#include <QList>
#include <QString>
#include <QWidget>

class QRubberBand;

// Energy spectra drawn as steps, with a legend and a logarithmic or linear count axis. Supports box zoom and moving
// the view. The mouse wheel and trackpad pinch zoom around the cursor, two-finger trackpad scrolling moves the view.
class SpectrumPlotWidget : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Zoom, Move };

    struct Curve {
        QString name; // In the legend and the cursor info
        QColor color;
        size_t events; // Shown in the legend
        std::vector<size_t> counts; // Bin i covers energies from i * binWidth to (i + 1) * binWidth
    };

    // What happens to the view when the curves are replaced
    enum class ViewChange {
        ShowAll,
        KeepEnergyRange, // The counts axis is scaled to show all counts
        Keep
    };

    explicit SpectrumPlotWidget(QWidget *parent = nullptr);
    void setCurves(double binWidth, const QList<Curve> &curves, ViewChange viewChange); // Bin width in MeV, as shown
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
    void drawCurve(QPainter &painter, const Curve &curve) const;
    void drawAxes(QPainter &painter, const QRect &plot) const;
    void drawLegend(QPainter &painter, const QRect &plot) const;

    QList<Curve> m_curves;
    double m_binWidth = 1; // MeV
    size_t m_bins = 0;
    size_t m_maxCount = 0;
    bool m_logScale;
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
