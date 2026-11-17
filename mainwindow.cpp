#include "mainwindow.h"

#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>

#include <gsl/gsl_version.h>
extern "C" {
#include <jibal.h>
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("JaRA");
    resize(1024, 768);

    auto *tabs = new QTabWidget(this);
    auto *placeholder = new QLabel("No data loaded.", tabs);
    placeholder->setAlignment(Qt::AlignCenter);
    tabs->addTab(placeholder, "Histogram");
    setCentralWidget(tabs);

    createMenus();
    statusBar()->showMessage(QString("JIBAL %1, GSL %2").arg(jibal_version(), GSL_VERSION));
}

void MainWindow::createMenus() {
    QMenu *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Open...", QKeySequence::Open, this, &MainWindow::openFile);
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);

    QMenu *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About JaRA", this, &MainWindow::about);
}

void MainWindow::openFile() {
    const QString fileName = QFileDialog::getOpenFileName(this, "Open ToF-ERDA data");
    if(!fileName.isEmpty()) {
        statusBar()->showMessage(QString("Opening files is not implemented yet (%1)").arg(fileName));
    }
}

void MainWindow::about() {
    QMessageBox::about(this, "About JaRA",
                       QString("<b>JaRA %1</b><p>Analysis of ToF-ERDA data.</p>"
                               "<p>JIBAL %2<br>GSL %3<br>Qt %4</p>")
                               .arg(JARA_VERSION, jibal_version(), GSL_VERSION, qVersion()));
}
