#include "elementdialog.h"
#include "colorbutton.h"
#include "defaults.h"
#include "elements.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLineEdit>
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
    m_typeCombo->setToolTip("Scattered ions are labeled with the target atom and an asterisk, their energy is calculated "
                            "with the mass of the incident ion.");
    m_form = new QFormLayout;
    m_form->addRow("Element:", m_edit);
    m_form->addRow("Isotope:", m_isotopeCombo);
    m_form->addRow("Color:", m_colorButton);
    m_form->setRowVisible(m_colorButton, false);
    m_form->addRow("Type:", m_typeCombo);
    m_form->setRowVisible(m_typeCombo, false);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(table);
    layout->addLayout(m_form);
    layout->addWidget(m_buttonBox);

    connect(m_elementGroup, &QButtonGroup::idClicked, this, &ElementDialog::elementClicked);
    connect(m_edit, &QLineEdit::textEdited, this, &ElementDialog::textEdited);
    connect(m_isotopeCombo, &QComboBox::activated, this, &ElementDialog::isotopeChanged);
    connect(m_colorButton, &ColorButton::colorPicked, this, [this]() { m_colorFollowsElement = false; });
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
    m_colorButton->setColor(followElement && m_z > 0 ? Defaults::elementColor(m_z) : color);
}

QColor ElementDialog::color() const {
    return m_colorButton->color();
}

void ElementDialog::setTypeSelectable(CutType type) {
    m_form->setRowVisible(m_typeCombo, true);
    m_typeCombo->setCurrentIndex(m_typeCombo->findData(static_cast<int>(type)));
}

CutType ElementDialog::type() const {
    return static_cast<CutType>(m_typeCombo->currentData().toInt());
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
    if(m_colorFollowsElement) {
        m_colorButton->setColor(m_z > 0 ? Defaults::elementColor(m_z) : QColor());
    }
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_z > 0 && (!m_requireIsotope || this->a() > 0));
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
}
