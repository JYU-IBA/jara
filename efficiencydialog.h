#ifndef JARA_EFFICIENCYDIALOG_H
#define JARA_EFFICIENCYDIALOG_H

#include <QDialog>

#include "project.h"

class QPushButton;
class QTreeWidget;

// Detection efficiencies of isotopes, imported from text files with the energy in MeV and the efficiency in two
// columns. The isotope is taken from the file name, e.g. 1H.eff or H-1.eff, or asked for.
class EfficiencyDialog : public QDialog {
    Q_OBJECT

public:
    explicit EfficiencyDialog(const QList<EfficiencyCurve> &curves, QWidget *parent = nullptr);
    const QList<EfficiencyCurve> &curves() const { return m_curves; }

private slots:
    void importFiles();
    void removeCurves();

private:
    void updateTable();

    QList<EfficiencyCurve> m_curves;
    QTreeWidget *m_table;
    QPushButton *m_removeButton;
};

// Reads an efficiency file, energies in MeV. Lines starting with '#' are skipped, columns after the second ignored.
bool readEfficiencyFile(const QString &fileName, QList<QPointF> &points, QString &errorMessage);

// "¹H, ⁷Li" or "None"
QString efficiencyText(const QList<EfficiencyCurve> &curves);

#endif // JARA_EFFICIENCYDIALOG_H
