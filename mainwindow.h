#ifndef JARA_MAINWINDOW_H
#define JARA_MAINWINDOW_H

#include <QMainWindow>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void openFile();
    void about();

private:
    void createMenus();
};

#endif // JARA_MAINWINDOW_H
