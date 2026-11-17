#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("JaRA");
    QApplication::setApplicationVersion(JARA_VERSION);
    MainWindow w;
    if(argc > 1) {
        w.openProjectFile(QString::fromLocal8Bit(argv[1]));
    }
    w.show();
    return QApplication::exec();
}
