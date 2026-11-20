#include "elementdialog.h"
#include "formlayout.h"
#include "colorbutton.h"
#include "defaults.h"
#include "elements.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr double MinimumAbundance = 1e-6; // Isotopes listed by default
}

ElementDialog::ElementDialog(int z, int a, QWidget *parent, bool requireIsotope)
    : QDialog(parent), m_requireIsotope(requireIsotope) {
    setWindowTitle(requireIsotope ? "Select Isotope" : "Select Element");

    auto *table = new QGridLayout;
    table->setSpacing(2);
    table->setRowMinimumHeight(Elements::LanthanideRow - 1, 8);
    m_elementGroup = new QButtonGroup(this);
    for(int i = 1; i <= std::min(Elements::maxZ(), 118); i++) {
        auto *button = new QToolButton;
        button->setText(Elements::symbol(i));
        button->setToolTip(QString("Z = %1").arg(i));
        button->setCheckable(true);
        button->setFixedSize(34, 28);
        m_elementGroup->addButton(button, i);
        const QPoint position = Elements::tablePosition(i);
        table->addWidget(button, position.y(), position.x());
    }

    m_edit = new QLineEdit;
    m_edit->setPlaceholderText("e.g. Si, 14 or 28Si");
    m_isotopeCombo = new QComboBox;
    m_colorButton = new ColorButton;
    m_typeCombo = new QComboBox;
    m_typeCombo->addItem("Recoil (ERD)", static_cast<int>(CutType::Recoil));
    m_typeCombo->addItem("Scattered (RBS)", static_cast<int>(CutType::Scattered));
    m_typeCombo->addItem("Reference", static_cast<int>(CutType::Reference));
    m_typeCombo->setToolTip("Scattered ions are labeled with the target atom and an asterisk, their energy is calculated "
                            "with the mass of the incident ion. Reference selections are not used in energy spectra or "
                            "depth profiles, they divide the measurement into parts with equal counts, their element "
                            "is optional.");
    m_representsCheck = new QCheckBox("Represents the natural element");
    m_representsCheck->setToolTip("In depth profiles, the events of the isotope are weighted by 1 / its natural "
                                  "abundance and the profile is labeled with the element. Only for isotopes that occur "
                                  "naturally.");
    m_abundanceSpin = new QDoubleSpinBox;
    m_abundanceSpin->setDecimals(6);
    m_abundanceSpin->setRange(1e-6, 100);
    m_abundanceSpin->setSuffix(" %");
    m_abundanceSpin->setToolTip("Natural abundance of the isotope used for the abundance correction");
    m_abundanceResetButton = new QToolButton;
    m_abundanceResetButton->setText("Use JIBAL");
    m_abundanceResetButton->setToolTip("Use the natural abundance from JIBAL");
    m_abundanceRow = new QWidget;
    auto *abundanceLayout = new QHBoxLayout(m_abundanceRow);
    abundanceLayout->setContentsMargins(0, 0, 0, 0);
    abundanceLayout->addWidget(m_abundanceSpin, 1);
    abundanceLayout->addWidget(m_abundanceResetButton);
    m_jibalAbundanceLabel = new QLabel;
    m_correctionLabel = new QLabel;
    m_correctionLabel->setToolTip("Weight of the events for the isotope standing for its element: 1 / natural abundance "
                                  "when the isotope represents its element, otherwise 1");
    m_weightSpin = new QDoubleSpinBox;
    m_weightSpin->setDecimals(3);
    m_weightSpin->setRange(MinCutWeight, MaxCutWeight);
    m_weightSpin->setSingleStep(0.1);
    m_weightSpin->setValue(1);
    m_weightSpin->setToolTip("The events of the selection are weighted by this in depth profiles, on top of the other "
                             "weights");
    m_form = createFormLayout();
    m_form->addRow("Element:", m_edit);
    m_form->addRow("Isotope:", m_isotopeCombo);
    m_form->addRow("Color:", m_colorButton);
    m_form->setRowVisible(m_colorButton, false);
    m_form->addRow("Type:", m_typeCombo);
    m_form->setRowVisible(m_typeCombo, false);
    m_form->addRow(QString(), m_representsCheck);
    m_form->addRow("Natural abundance:", m_abundanceRow);
    m_form->addRow("JIBAL abundance:", m_jibalAbundanceLabel);
    m_form->addRow("Abundance correction:", m_correctionLabel);
    m_form->addRow("Additional weight:", m_weightSpin);
    m_cutPropertyRows = {m_representsCheck, m_abundanceRow, m_jibalAbundanceLabel, m_correctionLabel, m_weightSpin};
    for(QWidget *widget : m_cutPropertyRows) {
        m_form->setRowVisible(widget, false);
    }

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(table);
    layout->addLayout(m_form);
    layout->addWidget(m_buttonBox);

    connect(m_elementGroup, &QButtonGroup::idClicked, this, &ElementDialog::elementClicked);
    connect(m_edit, &QLineEdit::textEdited, this, &ElementDialog::textEdited);
    connect(m_isotopeCombo, &QComboBox::activated, this, &ElementDialog::isotopeChanged);
    connect(m_colorButton, &ColorButton::colorPicked, this, [this]() { m_colorFollowsElement = false; });
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        updateAbundance();
        updateOkButton();
    });
    connect(m_representsCheck, &QCheckBox::toggled, this, &ElementDialog::updateAbundance);
    connect(m_abundanceSpin, &QDoubleSpinBox::valueChanged, this, [this]() {
        m_abundanceEdited = true;
        updateAbundance();
    });
    connect(m_abundanceResetButton, &QToolButton::clicked, this, [this]() {
        const QSignalBlocker blocker(m_abundanceSpin);
        m_abundanceSpin->setValue(Elements::abundance(m_z, this->a()) * 100);
        m_abundanceEdited = false;
        updateAbundance();
    });
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setElement(z, a);
    m_edit->setText(z > 0 ? Elements::label(z, a) : QString());
    m_edit->setFocus();
}

int ElementDialog::a() const {
    return m_isotopeCombo->currentData().toInt();
}

void ElementDialog::setColorSelectable(const QColor &color, bool followElement) {
    m_form->setRowVisible(m_colorButton, true);
    m_colorFollowsElement = followElement;
    m_colorButton->setColor(followElement ? Defaults::elementColor(m_z) : color);
}

QColor ElementDialog::color() const {
    return m_colorButton->color();
}

void ElementDialog::setTypeSelectable(CutType type) {
    m_form->setRowVisible(m_typeCombo, true);
    m_typeCombo->setCurrentIndex(m_typeCombo->findData(static_cast<int>(type)));
    updateOkButton();
}

CutType ElementDialog::type() const {
    return static_cast<CutType>(m_typeCombo->currentData().toInt());
}

void ElementDialog::setCutPropertiesSelectable(bool representsElement, std::optional<double> abundance, double weight) {
    for(QWidget *widget : m_cutPropertyRows) {
        m_form->setRowVisible(widget, true);
    }
    m_representsCheck->setChecked(representsElement && m_representsCheck->isEnabled());
    if(abundance) {
        const QSignalBlocker blocker(m_abundanceSpin);
        m_abundanceSpin->setValue(*abundance * 100);
        m_abundanceEdited = true;
    }
    m_weightSpin->setValue(weight);
    updateAbundance();
}

bool ElementDialog::representsElement() const {
    return canRepresentElement(m_z, a()) && type() != CutType::Reference && m_representsCheck->isChecked();
}

std::optional<double> ElementDialog::abundance() const {
    if(!m_abundanceEdited || !canRepresentElement(m_z, a())) {
        return std::nullopt;
    }
    return m_abundanceSpin->value() / 100;
}

double ElementDialog::weight() const {
    return m_weightSpin->value();
}

void ElementDialog::updateAbundance() {
    // The weights are only for depth profiles, which do not use references
    const bool analyzed = type() != CutType::Reference;
    m_representsCheck->setEnabled(analyzed && canRepresentElement(m_z, a()));
    m_weightSpin->setEnabled(analyzed);
    const bool represents = representsElement();
    m_abundanceRow->setEnabled(represents);
    m_abundanceResetButton->setEnabled(m_abundanceEdited);
    const double jibalAbundance = Elements::abundance(m_z, a());
    if(m_z <= 0 || a() == 0) {
        m_jibalAbundanceLabel->setText("No isotope selected");
    } else if(jibalAbundance <= 0) {
        m_jibalAbundanceLabel->setText("Does not occur naturally");
    } else {
        m_jibalAbundanceLabel->setText(QString("%1 %").arg(jibalAbundance * 100, 0, 'g', 6));
    }
    const double correction = represents ? 100 / m_abundanceSpin->value() : 1;
    m_correctionLabel->setText(QString::number(correction, 'g', 6));
}

void ElementDialog::setElement(int z, int a) {
    m_z = Elements::isKnown(z) ? z : 0;
    if(QAbstractButton *button = m_elementGroup->button(m_z)) {
        button->setChecked(true);
    } else if(QAbstractButton *checked = m_elementGroup->checkedButton()) {
        // An exclusive group does not allow unchecking its checked button
        m_elementGroup->setExclusive(false);
        checked->setChecked(false);
        m_elementGroup->setExclusive(true);
    }

    m_isotopeCombo->clear();
    if(m_z > 0) {
        if(m_requireIsotope && a == 0) {
            double maxAbundance = 0;
            for(const auto &isotope : Elements::isotopes(m_z)) {
                if(isotope.abundance > maxAbundance) {
                    maxAbundance = isotope.abundance;
                    a = isotope.a;
                }
            }
        }
        if(!m_requireIsotope) {
            m_isotopeCombo->addItem("Not specified", 0);
        }
        for(const auto &isotope : Elements::isotopes(m_z)) {
            // Only naturally occurring isotopes, unless another one was asked for explicitly
            if(isotope.abundance <= MinimumAbundance && isotope.a != a) {
                continue;
            }
            const QString name = Elements::label(m_z, isotope.a);
            m_isotopeCombo->addItem(isotope.abundance > 0 ? QString("%1 (%2 %)").arg(name).arg(isotope.abundance * 100, 0, 'g', 4)
                                                          : name,
                                    isotope.a);
        }
        m_isotopeCombo->setCurrentIndex(std::max(0, m_isotopeCombo->findData(a)));
    }
    m_isotopeCombo->setEnabled(m_z > 0);
    isotopeSelected();
    if(m_colorFollowsElement) {
        m_colorButton->setColor(Defaults::elementColor(m_z));
    }
    updateOkButton();
}

void ElementDialog::updateOkButton() {
    // Only reference selections can be without an element
    const bool elementOptional = m_form->isRowVisible(m_typeCombo) && type() == CutType::Reference;
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled((m_z > 0 || (elementOptional && m_edit->text().trimmed().isEmpty()))
                                                          && (!m_requireIsotope || a() > 0));
    m_edit->setPlaceholderText(elementOptional ? QStringLiteral("e.g. Si, 14 or 28Si, empty for none")
                                               : QStringLiteral("e.g. Si, 14 or 28Si"));
}

void ElementDialog::elementClicked(int z) {
    setElement(z, 0);
    m_edit->setText(m_requireIsotope ? Elements::label(z, a()) : Elements::symbol(z));
}

void ElementDialog::textEdited(const QString &text) {
    int z = 0;
    int a = 0;
    if(!Elements::parse(text, z, a)) {
        z = 0;
    }
    setElement(z, a);
}

void ElementDialog::isotopeChanged() {
    m_edit->setText(Elements::label(m_z, a()));
    isotopeSelected();
}

void ElementDialog::isotopeSelected() {
    if(!canRepresentElement(m_z, a())) {
        m_representsCheck->setChecked(false);
    }
    // A natural abundance set by the user is for the isotope it was set for
    {
        const QSignalBlocker blocker(m_abundanceSpin);
        m_abundanceSpin->setValue(Elements::abundance(m_z, a()) * 100);
        m_abundanceEdited = false;
    }
    updateAbundance();
}
