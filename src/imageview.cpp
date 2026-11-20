#include "imageview.h"

#include <algorithm>
#include <cmath>

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {
constexpr double ZoomStep = 1.25;
constexpr double MinZoom = 0.01;
constexpr double MaxZoom = 32;
}

// The image at a zoom, in a scroll area. Only the visible part is drawn, so that large photos can be zoomed in
// without scaling all of them. Reduced images are scaled smoothly once and kept.
class ImageCanvas : public QWidget {
public:
    QImage image;

    void setZoom(double zoom) {
        m_zoom = zoom;
        m_reduced = {};
        // Rounded down so that a fitted image does not bring up the scroll bars
        resize(std::max(1, static_cast<int>(image.width() * zoom)), std::max(1, static_cast<int>(image.height() * zoom)));
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        if(image.isNull()) {
            return;
        }
        QPainter painter(this);
        const qreal ratio = devicePixelRatioF();
        if(m_zoom * ratio < 1) {
            if(m_reduced.isNull()) {
                m_reduced = QPixmap::fromImage(image.scaled(size() * ratio, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
                m_reduced.setDevicePixelRatio(ratio);
            }
            painter.drawPixmap(0, 0, m_reduced);
            return;
        }
        // Enlarged pixels stay sharp
        const QRectF target = event->rect();
        const QRectF source(target.topLeft() / m_zoom, target.size() / m_zoom);
        painter.drawImage(target, image, source);
    }

private:
    double m_zoom = 1;
    QPixmap m_reduced;
};

bool isImageFile(const QString &path) {
    return QImageReader::supportedImageFormats().contains(QFileInfo(path).suffix().toLower().toUtf8());
}

ImageView::ImageView(Sample *sample, const QString &path, QWidget *parent) : QWidget(parent), m_sample(sample), m_path(path) {
    m_canvas = new ImageCanvas;
    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidget(m_canvas);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->viewport()->installEventFilter(this); // Refits when resized and zooms with the wheel

    auto createButton = [](const QString &text, const QString &toolTip) {
        auto *button = new QToolButton;
        button->setText(text);
        button->setToolTip(toolTip);
        return button;
    };
    QToolButton *zoomOutButton = createButton("−", "Zoom out (Ctrl/Cmd + wheel)");
    QToolButton *zoomInButton = createButton("+", "Zoom in (Ctrl/Cmd + wheel)");
    QToolButton *actualSizeButton = createButton("100 %", "Show the image in its own size");
    m_fitButton = createButton("Fit", "Fit the image to the window");
    m_fitButton->setCheckable(true);
    m_fitButton->setChecked(true);
    QToolButton *reloadButton = createButton("Reload", "Read the file again");
    QToolButton *openButton = createButton("Open Externally", "Open the file in the default application");
    m_infoLabel = new QLabel;
    m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *bar = new QHBoxLayout;
    for(QToolButton *button : {zoomOutButton, zoomInButton, actualSizeButton, m_fitButton}) {
        bar->addWidget(button);
    }
    bar->addWidget(m_infoLabel, 1);
    bar->addWidget(reloadButton);
    bar->addWidget(openButton);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(m_scrollArea, 1);

    connect(zoomOutButton, &QToolButton::clicked, this, [this]() { setZoom(m_zoom / ZoomStep); });
    connect(zoomInButton, &QToolButton::clicked, this, [this]() { setZoom(m_zoom * ZoomStep); });
    connect(actualSizeButton, &QToolButton::clicked, this, [this]() { setZoom(1); });
    connect(m_fitButton, &QToolButton::clicked, this, &ImageView::fit); // Unchecked by zooming, not by clicking
    connect(reloadButton, &QToolButton::clicked, this, &ImageView::load);
    connect(openButton, &QToolButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_path)); });
    load();
}

void ImageView::load() {
    QImageReader reader(m_path);
    reader.setAutoTransform(true);
    m_canvas->image = reader.read();
    if(m_canvas->image.isNull()) {
        m_canvas->setZoom(1);
        m_infoLabel->setText(QString("Could not read %1: %2").arg(QFileInfo(m_path).fileName(), reader.errorString()));
        return;
    }
    if(m_fit) {
        fit();
    } else {
        updateZoom();
    }
}

void ImageView::setZoom(double zoom) {
    if(m_canvas->image.isNull()) {
        return;
    }
    // The point in the middle of the window stays there
    QScrollBar *h = m_scrollArea->horizontalScrollBar();
    QScrollBar *v = m_scrollArea->verticalScrollBar();
    const QSize viewport = m_scrollArea->viewport()->size();
    const double x = (h->value() + viewport.width() / 2.0) / m_zoom;
    const double y = (v->value() + viewport.height() / 2.0) / m_zoom;
    m_zoom = std::clamp(zoom, MinZoom, MaxZoom);
    m_fit = false;
    m_fitButton->setChecked(false);
    updateZoom();
    h->setValue(static_cast<int>(std::lround(x * m_zoom - viewport.width() / 2.0)));
    v->setValue(static_cast<int>(std::lround(y * m_zoom - viewport.height() / 2.0)));
}

void ImageView::fit() {
    m_fit = true;
    m_fitButton->setChecked(true);
    const QImage &image = m_canvas->image;
    if(image.isNull()) {
        return;
    }
    const QSize viewport = m_scrollArea->viewport()->size();
    m_zoom = std::min({static_cast<double>(viewport.width()) / image.width(),
                       static_cast<double>(viewport.height()) / image.height(), 1.0}); // Small images are not enlarged
    updateZoom();
}

void ImageView::updateZoom() {
    m_canvas->setZoom(m_zoom);
    const QImage &image = m_canvas->image;
    m_infoLabel->setText(QString("%1 × %2 pixels, %3 %").arg(image.width()).arg(image.height()).arg(std::lround(m_zoom * 100)));
}

bool ImageView::eventFilter(QObject *watched, QEvent *event) {
    if(watched == m_scrollArea->viewport()) {
        if(event->type() == QEvent::Resize && m_fit) {
            fit();
        } else if(event->type() == QEvent::Wheel) {
            auto *wheel = static_cast<QWheelEvent *>(event);
            if(wheel->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
                if(const int delta = wheel->angleDelta().y(); delta != 0) {
                    setZoom(delta > 0 ? m_zoom * ZoomStep : m_zoom / ZoomStep);
                }
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
