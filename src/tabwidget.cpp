/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "tabwidget.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QAbstractButton>
#include <QStyle>

namespace {
// The dragged tab is only known inside this process, the data identifies the process
const QString TabMimeType = QStringLiteral("application/x-jara-tab");

struct TabDrag {
    QPointer<DetachableTabWidget> source;
    QPointer<QWidget> page;
    bool dropped = false;
};
TabDrag currentDrag;

bool acceptsDrag(const QMimeData *mimeData) {
    return currentDrag.page && mimeData->hasFormat(TabMimeType)
           && mimeData->data(TabMimeType) == QByteArray::number(QCoreApplication::applicationPid());
}

#ifdef Q_OS_MACOS
// A thin cross in the text color, on a rounded background while the mouse is over it, like the close buttons of
// Safari's tabs
class TabCloseButton : public QAbstractButton {
public:
    TabCloseButton() {
        setAttribute(Qt::WA_Hover); // Repainted when the mouse enters and leaves
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::ArrowCursor);
        setToolTip("Close Tab");
    }
    QSize sizeHint() const override { return {16, 16}; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor color = palette().color(QPalette::WindowText);
        if(isDown() || underMouse()) {
            QColor background = color;
            background.setAlphaF(isDown() ? 0.25f : 0.13f);
            painter.setPen(Qt::NoPen);
            painter.setBrush(background);
            painter.drawRoundedRect(QRectF(rect()), 3, 3);
        } else {
            color.setAlphaF(0.6f);
        }
        painter.setPen(QPen(color, 1.25, Qt::SolidLine, Qt::RoundCap));
        const QRectF cross = QRectF(0, 0, 7, 7).translated(QRectF(rect()).center() - QPointF(3.5, 3.5));
        painter.drawLine(cross.topLeft(), cross.bottomRight());
        painter.drawLine(cross.topRight(), cross.bottomLeft());
    }
};
#endif
}

DetachableTabBar::DetachableTabBar(QWidget *parent) : QTabBar(parent) {
}

void DetachableTabBar::mousePressEvent(QMouseEvent *event) {
    m_pressed = event->button() == Qt::LeftButton && tabAt(event->pos()) >= 0;
    QTabBar::mousePressEvent(event);
}

void DetachableTabBar::mouseMoveEvent(QMouseEvent *event) {
    // Moving well away from the bar, mostly vertically, starts a drag of the pressed tab
    const int margin = height();
    if(m_pressed && (event->buttons() & Qt::LeftButton) && !rect().adjusted(-margin, -margin, margin, margin).contains(event->pos())) {
        m_pressed = false;
        // Ends the tab bar's own moving of the tab before the drag starts. The pressed tab is the current one.
        QMouseEvent release(QEvent::MouseButtonRelease, event->position(), event->globalPosition(), Qt::LeftButton,
                            Qt::NoButton, event->modifiers());
        QTabBar::mouseReleaseEvent(&release);
        emit dragOutRequested(currentIndex());
        return;
    }
    QTabBar::mouseMoveEvent(event);
}

EmptyTab::EmptyTab(QWidget *parent)
    : QLabel("Click an item in the project tree to open it here.\n"
             "Ctrl-click (⌘-click) or middle-click opens it in a new tab.", parent) {
    setAlignment(Qt::AlignCenter);
    setWordWrap(true);
    setForegroundRole(QPalette::PlaceholderText);
}

DetachableTabWidget::DetachableTabWidget(const QString &emptyHint, QWidget *parent)
    : QTabWidget(parent), m_emptyHint(emptyHint) {
    auto *bar = new DetachableTabBar;
    setTabBar(bar);
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    setAcceptDrops(true);
    connect(bar, &DetachableTabBar::dragOutRequested, this, &DetachableTabWidget::startDrag);
}

void DetachableTabWidget::setTabTitle(int index, const QString &title) {
    setTabText(index, title);
    emit titlesChanged();
}

void DetachableTabWidget::activateTab(int index) {
    setCurrentIndex(index);
    window()->show();
    window()->raise();
    window()->activateWindow();
}

void DetachableTabWidget::moveTab(DetachableTabWidget *from, int fromIndex, DetachableTabWidget *to, int toIndex) {
    QWidget *page = from->widget(fromIndex);
    const QString text = from->tabText(fromIndex);
    const QString toolTip = from->tabToolTip(fromIndex);
    const QIcon icon = from->tabIcon(fromIndex);
    from->removeTab(fromIndex);
    const int index = to->insertTab(toIndex, page, icon, text);
    to->setTabToolTip(index, toolTip);
    to->activateTab(index);
}

bool DetachableTabWidget::isDragging() {
    return !currentDrag.page.isNull();
}

void DetachableTabWidget::tabInserted(int index) {
    QTabWidget::tabInserted(index);
#ifdef Q_OS_MACOS
    // The macOS style draws the close button of a tab in document mode only while the mouse is over the tab (and keeps
    // the last one hovered), so the tabs get close buttons of their own that are always shown
    const auto side = static_cast<QTabBar::ButtonPosition>(style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, tabBar()));
    auto *button = new TabCloseButton;
    button->resize(button->sizeHint());
    connect(button, &QAbstractButton::clicked, this, [this, button, side]() {
        for(int i = 0; i < count(); i++) {
            if(tabBar()->tabButton(i, side) == button) {
                emit tabCloseRequested(i);
                return;
            }
        }
    });
    if(QWidget *previous = tabBar()->tabButton(index, side)) {
        previous->deleteLater();
    }
    tabBar()->setTabButton(index, side, button);
#endif
    emit countChanged(count());
}

void DetachableTabWidget::tabRemoved(int index) {
    QTabWidget::tabRemoved(index);
    emit countChanged(count());
}

void DetachableTabWidget::paintEvent(QPaintEvent *event) {
    QTabWidget::paintEvent(event);
    if(count() == 0 && !m_emptyHint.isEmpty()) {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap, m_emptyHint);
    }
}

void DetachableTabWidget::startDrag(int index) {
    QWidget *page = widget(index);
    if(!page) {
        return;
    }
    currentDrag = {this, page, false};
    auto *mimeData = new QMimeData;
    mimeData->setData(TabMimeType, QByteArray::number(QCoreApplication::applicationPid()));
    auto *drag = new QDrag(this);
    drag->setMimeData(mimeData);
    // A small picture of the page follows the cursor
    const QPixmap picture = page->grab();
    if(!picture.isNull()) {
        QPixmap thumbnail = picture.scaledToWidth(std::min(320, picture.width()), Qt::SmoothTransformation);
        QPixmap faded(thumbnail.size());
        faded.fill(Qt::transparent);
        QPainter painter(&faded);
        painter.setOpacity(0.7);
        painter.drawPixmap(0, 0, thumbnail);
        painter.end();
        drag->setPixmap(faded);
        drag->setHotSpot(QPoint(faded.width() / 2, 0));
    }
    drag->exec(Qt::MoveAction);
    const bool dropped = currentDrag.dropped;
    currentDrag = {};

    // Not dropped on any tab widget: the page gets a window of its own, unless it already is alone in one
    const int pageIndex = indexOf(page);
    if(!dropped && pageIndex >= 0) {
        if(count() == 1 && qobject_cast<TabWindow *>(window())) {
            window()->move(QCursor::pos() - QPoint(window()->width() / 2, 10));
        } else {
            emit detachRequested(pageIndex, QCursor::pos());
        }
    }
    emit countChanged(count()); // Windows left empty by the drag close now
}

int DetachableTabWidget::dropIndex(const QPoint &pos) const {
    const QPoint barPos = tabBar()->mapFrom(this, pos);
    if(barPos.y() < 0 || barPos.y() >= tabBar()->height()) {
        return count(); // Not on the tab bar, added last
    }
    const int index = tabBar()->tabAt(barPos);
    return index >= 0 ? index : count();
}

void DetachableTabWidget::dragEnterEvent(QDragEnterEvent *event) {
    if(acceptsDrag(event->mimeData())) {
        event->acceptProposedAction();
    }
}

void DetachableTabWidget::dragMoveEvent(QDragMoveEvent *event) {
    if(acceptsDrag(event->mimeData())) {
        event->acceptProposedAction();
    }
}

void DetachableTabWidget::dropEvent(QDropEvent *event) {
    if(!acceptsDrag(event->mimeData()) || !currentDrag.source) {
        return;
    }
    DetachableTabWidget *source = currentDrag.source;
    const int fromIndex = source->indexOf(currentDrag.page);
    if(fromIndex < 0) {
        return;
    }
    const int toIndex = dropIndex(event->position().toPoint());
    if(source == this) {
        tabBar()->moveTab(fromIndex, std::min(toIndex, count() - 1));
    } else {
        moveTab(source, fromIndex, this, toIndex);
    }
    currentDrag.dropped = true;
    event->acceptProposedAction();
}

TabWindow::TabWindow(QWidget *parent) : QMainWindow(parent, Qt::Window) {
    setAttribute(Qt::WA_DeleteOnClose);
    m_tabs = new DetachableTabWidget;
    setCentralWidget(m_tabs);
    connect(m_tabs, &DetachableTabWidget::currentChanged, this, &TabWindow::updateTitle);
    connect(m_tabs, &DetachableTabWidget::titlesChanged, this, &TabWindow::updateTitle);
    // Queued, and not during a drag, so that the window is not deleted while its tab bar is handling the drag
    connect(m_tabs, &DetachableTabWidget::countChanged, this, [this](int count) {
        if(count == 0 && !DetachableTabWidget::isDragging()) {
            close();
        }
    }, Qt::QueuedConnection);
}

void TabWindow::updateTitle() {
    const int index = m_tabs->currentIndex();
    setWindowTitle(index >= 0 ? QString("%1 - JaRA").arg(m_tabs->tabText(index)) : QStringLiteral("JaRA"));
}
