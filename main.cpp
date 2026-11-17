#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("JaRA");
    QApplication::setApplicationVersion(JARA_VERSION);
    MainWindow w;
    w.show();
    return QApplication::exec();
}
