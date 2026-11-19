#ifndef JARA_ELEMENTDIALOG_H
#define JARA_ELEMENTDIALOG_H

#include <optional>

#include <QDialog>
#include <QList>

#include "project.h"

class ColorButton;
class QButtonGroup;
class QCheckBox;
class QColor;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
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
    void setTypeSelectable(CutType type); // Shows a row for choosing between recoils and scattered ions
    CutType type() const;
    // Shows the rows of the rest of a selection's properties, see Cut: whether the isotope represents its natural
    // element, its natural abundance and the additional weight of the events
    void setCutPropertiesSelectable(bool representsElement, std::optional<double> abundance, double weight);
    bool representsElement() const; // False when the isotope cannot represent its element
    std::optional<double> abundance() const; // Set by the user, nullopt for the one from JIBAL
    double weight() const;

private slots:
    void elementClicked(int z);
    void textEdited(const QString &text);
    void isotopeChanged();

private:
    void setElement(int z, int a);
    void isotopeSelected(); // Resets the natural abundance to the one from JIBAL
    void updateAbundance(); // Shows the abundance correction and enables the rows of representing the element

    int m_z = 0;
    bool m_requireIsotope;
    bool m_colorFollowsElement = false;
    bool m_abundanceEdited = false; // The abundance was set by the user instead of taken from JIBAL
    QLineEdit *m_edit;
    QComboBox *m_isotopeCombo;
    ColorButton *m_colorButton;
    QComboBox *m_typeCombo;
    QCheckBox *m_representsCheck;
    QWidget *m_abundanceRow;
    QDoubleSpinBox *m_abundanceSpin; // %
    QToolButton *m_abundanceResetButton;
    QLabel *m_jibalAbundanceLabel;
    QLabel *m_correctionLabel;
    QDoubleSpinBox *m_weightSpin;
    QList<QWidget *> m_cutPropertyRows; // Shown by setCutPropertiesSelectable()
    QFormLayout *m_form;
    QDialogButtonBox *m_buttonBox;
    QButtonGroup *m_elementGroup;
};

#endif // JARA_ELEMENTDIALOG_H
