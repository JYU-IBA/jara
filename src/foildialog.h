#ifndef JARA_FOILDIALOG_H
#define JARA_FOILDIALOG_H

#include <QDialog>

#include "project.h"

class MaterialEditor;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;

// Material and thickness of the foil of the first timing detector. The thickness can be given as mass or as atoms per
// area. When the material changes, the thickness in the shown unit is kept.
class FoilDialog : public QDialog {
    Q_OBJECT

public:
    FoilDialog(const Material &material, double thickness, QWidget *parent = nullptr); // Thickness in kg/m²
    Material material() const;
    double thickness() const { return m_thickness; } // kg/m²

private:
    double atomMass() const; // kg, average of the edited material, zero when it has no elements
    double unitSize() const; // Of the selected unit in kg/m², zero when it cannot be converted
    void showThickness();
    void updateState();

    double m_thickness;
    MaterialEditor *m_editor;
    QDoubleSpinBox *m_thicknessSpin;
    QComboBox *m_unitCombo;
    QLabel *m_conversionLabel;
    QDialogButtonBox *m_buttonBox;
};

// "C, 3 µg/cm²"
QString foilText(const Material &material, double thickness);

#endif // JARA_FOILDIALOG_H
