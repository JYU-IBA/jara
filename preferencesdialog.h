#ifndef JARA_PREFERENCESDIALOG_H
#define JARA_PREFERENCESDIALOG_H

#include <QColor>
#include <QDialog>
#include <QMap>

#include "project.h"

class QButtonGroup;

// Edits the program-wide default experimental and detector settings and the default colors of elements. They are
// stored when the dialog is accepted.
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
    void accept() override;

private:
    QWidget *createSettingsPage();
    QWidget *createElementColorsPage();
    void updateElementButton(int z);

    ExperimentalSettings m_experimental;
    DetectorSettings m_detector;
    QMap<int, QColor> m_elementColors; // Colors changed by the user, by Z
    QButtonGroup *m_elementGroup = nullptr;
};

#endif // JARA_PREFERENCESDIALOG_H
