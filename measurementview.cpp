#include "measurementview.h"
#include "histogramwidget.h"
#include "project.h"

#include <QApplication>
#include <QLabel>
#include <QVBoxLayout>

MeasurementView::MeasurementView(Measurement *measurement, QWidget *parent) : QWidget(parent), m_measurement(measurement) {
    auto *layout = new QVBoxLayout(this);

    QString errorMessage;
    bool loaded = false;
    if(measurement->path.isEmpty()) {
        errorMessage = "The measurement has no data file.";
    } else {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        loaded = loadAscFile(measurement->path, m_events, errorMessage);
        QApplication::restoreOverrideCursor();
    }
    if(!loaded) {
        auto *label = new QLabel(QString("Could not load %1:\n%2").arg(measurement->path, errorMessage));
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        layout->addWidget(label);
        return;
    }

    auto *infoLabel = new QLabel(QString("%1 — %2 events").arg(measurement->path).arg(m_events.size()));
    infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *histogram = new HistogramWidget;
    histogram->setEvents(&m_events);
    const QString hint = QStringLiteral("Drag to zoom, double-click to reset.");
    auto *cursorLabel = new QLabel(hint);

    layout->addWidget(infoLabel);
    layout->addWidget(histogram, 1);
    layout->addWidget(cursorLabel);

    connect(histogram, &HistogramWidget::cursorInfoChanged, cursorLabel, [cursorLabel, hint](const QString &text) {
        cursorLabel->setText(text.isEmpty() ? hint : text);
    });
}
