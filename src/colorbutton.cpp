#include "colorbutton.h"

#include <QColorDialog>
#include <QPainter>
#include <QPixmap>

ColorButton::ColorButton(QWidget *parent) : QToolButton(parent) {
    setIconSize({32, 16});
    connect(this, &QToolButton::clicked, this, &ColorButton::pickColor);
}

void ColorButton::setColor(const QColor &color) {
    m_color = color;
    if(!color.isValid()) {
        setIcon({});
        return;
    }
    QPixmap pixmap(iconSize());
    pixmap.fill(color);
    QPainter painter(&pixmap);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    setIcon(pixmap);
}

void ColorButton::pickColor() {
    const QColor color = QColorDialog::getColor(m_color.isValid() ? m_color : Qt::white, this, "Select Color");
    if(color.isValid()) {
        setColor(color);
        emit colorPicked(color);
    }
}

QIcon colorSwatch(const QColor &color, int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(color.darker(150));
    painter.setBrush(color);
    painter.drawRect(QRectF(0.5, 0.5, size - 1, size - 1));
    return pixmap;
}
