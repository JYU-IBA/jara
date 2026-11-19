#ifndef JARA_TABWIDGET_H
#define JARA_TABWIDGET_H

#include <QMainWindow>
#include <QTabBar>
#include <QTabWidget>

// Tab bar that asks for a drag when a tab is dragged away from the bar. Dragging along the bar reorders tabs.
class DetachableTabBar : public QTabBar {
    Q_OBJECT

public:
    explicit DetachableTabBar(QWidget *parent = nullptr);

signals:
    void dragOutRequested(int index);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    bool m_pressed = false;
};

// Tabs that can be dragged to another DetachableTabWidget, in this or another window, or out of all of them to a new
// window. Shows a hint when there are no tabs.
class DetachableTabWidget : public QTabWidget {
    Q_OBJECT

public:
    explicit DetachableTabWidget(const QString &emptyHint = {}, QWidget *parent = nullptr);
    void setTabTitle(int index, const QString &title); // Use instead of setTabText() so that windows see the change
    void activateTab(int index); // Makes it current and brings its window to the front
    static void moveTab(DetachableTabWidget *from, int fromIndex, DetachableTabWidget *to, int toIndex);
    static bool isDragging();

signals:
    void detachRequested(int index, const QPoint &globalPos); // Dropped outside of all tab widgets
    void titlesChanged();
    void countChanged(int count);

protected:
    void tabInserted(int index) override;
    void tabRemoved(int index) override;
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void startDrag(int index);
    int dropIndex(const QPoint &pos) const; // Tab position for a drop at pos, in this widget's coordinates

    QString m_emptyHint;
};

// Window for tabs dragged out of the main window. Closes itself when its last tab is moved away or closed.
class TabWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit TabWindow(QWidget *parent);
    DetachableTabWidget *tabs() const { return m_tabs; }

private:
    void updateTitle();

    DetachableTabWidget *m_tabs;
};

#endif // JARA_TABWIDGET_H
