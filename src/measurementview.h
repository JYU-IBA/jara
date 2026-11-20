#ifndef JARA_MEASUREMENTVIEW_H
#define JARA_MEASUREMENTVIEW_H

#include <vector>

#include <QList>
#include <QPointF>
#include <QWidget>

#include "project.h"
#include "tofedata.h"

class HistogramWidget;
class QAction;
class QLabel;
class QListWidget;

// Tab in the main work area where a measurement is worked with. Loads the measurement's data file when created.
class MeasurementView : public QWidget {
    Q_OBJECT

public:
    explicit MeasurementView(Measurement *measurement, QWidget *parent = nullptr);
    Measurement *measurement() const { return m_measurement; }

signals:
    void modified();

private slots:
    void polygonCompleted(const QList<QPointF> &polygon);
    void editCut(); // All properties of the current selection
    void changeCutColor();
    void resetCutColors(); // All selections get the colors of their elements from the preferences
    void setCutType(CutType type); // Of the selected cuts
    void setRepresentsElement(bool represents); // Of the selected cuts that can represent their element
    void setReferenceCut(int row); // Divides the measurement, -1 for none (all events)
    void removeCuts();
    void copyCuts();
    void pasteCuts();

private:
    QWidget *createToolBar();
    QWidget *createCutPanel();
    void updateCutList();
    // Sorts the selections after they were added or their elements changed, the current and the selected ones by id
    // stay so
    void sortCuts(int currentId, const QList<int> &selectedIds);
    QList<int> selectedCutRows() const; // Ascending

    Measurement *m_measurement;
    std::vector<ToFEEvent> m_events;
    HistogramWidget *m_histogram = nullptr;
    QListWidget *m_cutList = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QLabel *m_statusLabel = nullptr;
    QString m_hint;
};

#endif // JARA_MEASUREMENTVIEW_H
