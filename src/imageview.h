#ifndef JARA_IMAGEVIEW_H
#define JARA_IMAGEVIEW_H

#include <QWidget>

class ImageCanvas;
class QLabel;
class QScrollArea;
class QToolButton;
struct Sample;

// True for the files that ImageView can show (PNG, JPEG, ...), by their suffix
bool isImageFile(const QString &path);

// Tab in the main work area with an image attached to a sample, e.g. a photo of it. Fits the window by default and
// zooms with the buttons or Ctrl/Cmd and the mouse wheel. Photos are turned upright by their EXIF orientation.
class ImageView : public QWidget {
    Q_OBJECT

public:
    ImageView(Sample *sample, const QString &path, QWidget *parent = nullptr);
    Sample *sample() const { return m_sample; }
    QString path() const { return m_path; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void load();
    void setZoom(double zoom); // 1 is the image's own size
    void fit();
    void updateZoom(); // Of the canvas and the label

    Sample *m_sample;
    QString m_path;
    double m_zoom = 1;
    bool m_fit = true; // Follows the size of the window
    QScrollArea *m_scrollArea;
    ImageCanvas *m_canvas;
    QLabel *m_infoLabel;
    QToolButton *m_fitButton;
};

#endif // JARA_IMAGEVIEW_H
