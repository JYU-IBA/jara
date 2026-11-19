#ifndef JARA_COLORBUTTON_H
#define JARA_COLORBUTTON_H

#include <QColor>
#include <QIcon>
#include <QToolButton>

// Shows a color, clicking it picks another one with a color dialog
class ColorButton : public QToolButton {
    Q_OBJECT

public:
    explicit ColorButton(QWidget *parent = nullptr);
    QColor color() const { return m_color; }
    void setColor(const QColor &color); // An invalid color shows no color

signals:
    void colorPicked(const QColor &color); // Only when the user picks a color

private:
    void pickColor();

    QColor m_color;
};

QIcon colorSwatch(const QColor &color, int size = 16); // Square filled with the color

#endif // JARA_COLORBUTTON_H
