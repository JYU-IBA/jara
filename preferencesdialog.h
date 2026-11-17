#ifndef JARA_PREFERENCESDIALOG_H
#define JARA_PREFERENCESDIALOG_H

#include <QDialog>

#include "project.h"

// Edits the program-wide default experimental and detector settings. They are stored when the dialog is accepted.
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
    void accept() override;

private:
    ExperimentalSettings m_experimental;
    DetectorSettings m_detector;
};

#endif // JARA_PREFERENCESDIALOG_H
