#ifndef JARA_ELEMENTDIALOG_H
#define JARA_ELEMENTDIALOG_H

#include <QDialog>

class ColorButton;
class QButtonGroup;
class QColor;
class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLineEdit;
class QToolButton;

// Selects an element from the periodic table or by typing, and optionally an isotope of it
class ElementDialog : public QDialog {
    Q_OBJECT

public:
    // With requireIsotope, a specific isotope must be selected, the most abundant one is preselected
    explicit ElementDialog(int z = 0, int a = 0, QWidget *parent = nullptr, bool requireIsotope = false);
    int z() const { return m_z; }
    int a() const; // Zero when no specific isotope is selected
    // Shows a color row. With followElement, the color is the default color of the selected element until the user
    // picks one.
    void setColorSelectable(const QColor &color, bool followElement);
    QColor color() const;

private slots:
    void elementClicked(int z);
    void textEdited(const QString &text);
    void isotopeChanged();

private:
    void setElement(int z, int a);

    int m_z = 0;
    bool m_requireIsotope;
    bool m_colorFollowsElement = false;
    QLineEdit *m_edit;
    QComboBox *m_isotopeCombo;
    ColorButton *m_colorButton;
    QFormLayout *m_form;
    QDialogButtonBox *m_buttonBox;
    QButtonGroup *m_elementGroup;
};

#endif // JARA_ELEMENTDIALOG_H
