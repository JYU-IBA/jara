#include <QApplication>
#include <QMessageBox>

#include "elements.h"
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("JaRA");
    QApplication::setApplicationVersion(JARA_VERSION);
    QString errorMessage;
    if(!Elements::init(errorMessage)) {
        QMessageBox::warning(nullptr, "JaRA", QString("Element data could not be loaded from JIBAL:\n%1").arg(errorMessage));
    }
    MainWindow w;
    if(argc > 1) {
        w.openProjectFile(QString::fromLocal8Bit(argv[1]));
    }
    w.show();
    const int result = QApplication::exec();
    Elements::cleanup();
    return result;
}
