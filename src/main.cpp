#include <QApplication>
#include <QIcon>
#include <QMessageBox>

#include "elements.h"
#include "mainwindow.h"
#include "version.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("JaRA");
    QApplication::setOrganizationName("JYU");
    QApplication::setOrganizationDomain("jyu.fi"); // Settings are stored as fi.jyu.JaRA on macOS
    QApplication::setApplicationVersion(Version::version());
#ifndef Q_OS_MACOS // The bundle icon is used on macOS, the window icon would replace it in the Dock
    QIcon icon;
    for(int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(QString(":/icons/jara_%1.png").arg(size));
    }
    QApplication::setWindowIcon(icon);
#endif
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
