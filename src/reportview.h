#ifndef JARA_REPORTVIEW_H
#define JARA_REPORTVIEW_H

#include <QStringList>
#include <QWidget>

class Project;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;
struct PlotView;
struct Report;

// Tab in the main work area where a report is set up and its PDF generated, see reportpdf.h. The depth profiles and
// split analyses of the project are listed with check boxes. The report is marked out of date when anything it shows
// has changed since its PDF was generated.
class ReportView : public QWidget {
    Q_OBJECT

public:
    ReportView(Project *project, Report *report, QWidget *parent = nullptr);
    Report *report() const { return m_report; }
    QString title() const; // The report's name, marked when out of date
    bool isStale() const { return m_stale; }

public slots:
    void refresh(); // Call after the project has changed: the items listed and whether the report is out of date

signals:
    void modified();
    void titleChanged();

private slots:
    void generatePdf();
    void contentItemChanged(QTreeWidgetItem *item);

private:
    QWidget *createOptions();
    QWidget *createStaleBar();
    void updateContents(); // Rebuilt only when the project's items change
    void updateRangeControls();
    void updateViewTable(); // Rows of the included profiles with RangeMode::PerProfile
    void changed(); // An option was changed: the project is modified, later, outside the handlers of the widgets
    void updateStale();

    Project *m_project;
    Report *m_report;
    bool m_stale = false;
    QStringList m_contentsKey; // Items listed, the list is rebuilt when they change
    QStringList m_viewTableKey;
    QTreeWidget *m_contents = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QComboBox *m_namesCombo = nullptr;
    QComboBox *m_rangeCombo = nullptr;
    QWidget *m_commonRange = nullptr;
    QDoubleSpinBox *m_depthMinSpin = nullptr; // Of the common range, 1e15 at/cm²
    QDoubleSpinBox *m_depthMaxSpin = nullptr;
    QDoubleSpinBox *m_concentrationMinSpin = nullptr; // at.%
    QDoubleSpinBox *m_concentrationMaxSpin = nullptr;
    QCheckBox *m_logCheck = nullptr;
    QTableWidget *m_viewTable = nullptr;
    QWidget *m_staleBar = nullptr;
    QLabel *m_statusLabel = nullptr;
};

#endif // JARA_REPORTVIEW_H
