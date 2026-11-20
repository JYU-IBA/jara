#include "foildialog.h"
#include "materialeditor.h"

#include <algorithm>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
enum Unit { MassUnit, AtomUnit };
const QString UnitKey = QStringLiteral("foilDialog/unit"); // The unit last used is remembered
const QString MassUnitText = QStringLiteral("µg/cm²");
const QString AtomUnitText = QStringLiteral("10¹⁵ at/cm²");
const QString AtomValueText = QStringLiteral("e15 at/cm²"); // After a number, "20e15 at/cm²"
}

QString foilText(const Material &material, double thickness) {
    return QString("%1, %2 %3").arg(materialText(material))
            .arg(thickness / Units::MicrogramPerSquareCentimetre, 0, 'g', 4).arg(MassUnitText);
}

FoilDialog::FoilDialog(const Material &material, double thickness, QWidget *parent)
    : QDialog(parent), m_thickness(thickness) {
    setWindowTitle("Timing Detector Foil");
    m_editor = new MaterialEditor;
    m_editor->setMaterial(material);
    m_thicknessSpin = new QDoubleSpinBox;
    m_thicknessSpin->setDecimals(4);
    m_thicknessSpin->setRange(0, 1e6);
    m_unitCombo = new QComboBox;
    m_unitCombo->addItem(MassUnitText);
    m_unitCombo->addItem(AtomUnitText);
    m_unitCombo->setCurrentIndex(std::clamp(QSettings().value(UnitKey, MassUnit).toInt(), 0, 1));
    m_conversionLabel = new QLabel;
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto *thicknessRow = new QHBoxLayout;
    thicknessRow->addWidget(m_thicknessSpin, 1);
    thicknessRow->addWidget(m_unitCombo);
    auto *form = new QFormLayout;
    form->addRow("Thickness:", thicknessRow);
    form->addRow(QString(), m_conversionLabel);
    auto *note = new QLabel("Particles lose energy in the foil of the first timing detector before their time of "
                            "flight is measured.");
    note->setWordWrap(true);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addWidget(m_editor, 1);
    layout->addLayout(form);
    layout->addWidget(m_buttonBox);

    connect(m_thicknessSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        m_thickness = value * unitSize();
        updateState();
    });
    connect(m_unitCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        QSettings().setValue(UnitKey, index);
        showThickness();
    });
    connect(m_editor, &MaterialEditor::changed, this, [this]() {
        if(m_unitCombo->currentIndex() == AtomUnit) {
            m_thickness = m_thicknessSpin->value() * unitSize(); // The number of atoms is kept
        }
        updateState();
    });
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    showThickness();
    resize(460, 420);
}

Material FoilDialog::material() const {
    return m_editor->material();
}

double FoilDialog::atomMass() const {
    const Material &material = m_editor->material();
    double sum = 0;
    for(const MaterialElement &element : material.elements) {
        sum += element.fraction;
    }
    return sum > 0 ? averageAtomMass(material) / sum : 0;
}

double FoilDialog::unitSize() const {
    return m_unitCombo->currentIndex() == MassUnit ? Units::MicrogramPerSquareCentimetre : Units::ThinFilmUnit * atomMass();
}

void FoilDialog::showThickness() {
    const double size = unitSize();
    const QSignalBlocker blocker(m_thicknessSpin);
    m_thicknessSpin->setValue(size > 0 ? m_thickness / size : 0);
    updateState();
}

void FoilDialog::updateState() {
    const double mass = atomMass();
    if(mass <= 0) {
        m_conversionLabel->clear();
    } else if(m_unitCombo->currentIndex() == MassUnit) {
        m_conversionLabel->setText(QString("= %1%2").arg(m_thickness / (Units::ThinFilmUnit * mass), 0, 'g', 5).arg(AtomValueText));
    } else {
        m_conversionLabel->setText(QString("= %1 %2").arg(m_thickness / Units::MicrogramPerSquareCentimetre, 0, 'g', 5).arg(MassUnitText));
    }
    // The thickness in atoms needs the material, which also must be complete to be used
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_editor->isValid());
}
