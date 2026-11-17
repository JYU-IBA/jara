#include "elementdialog.h"
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
constexpr int LanthanideRow = 8; // Lanthanides and actinides are shown below the main table, after a gap row

// Column (x) and row (y) of an element in the periodic table
QPoint tablePosition(int z) {
    if(z == 1) {
        return {0, 0};
    }
    if(z == 2) {
        return {17, 0};
    }
    if(z <= 10) {
        return z <= 4 ? QPoint(z - 3, 1) : QPoint(z - 5 + 12, 1);
    }
    if(z <= 18) {
        return z <= 12 ? QPoint(z - 11, 2) : QPoint(z - 13 + 12, 2);
    }
    if(z <= 36) {
        return {z - 19, 3};
    }
    if(z <= 54) {
        return {z - 37, 4};
    }
    if(z <= 56) {
        return {z - 55, 5};
    }
    if(z <= 71) {
        return {z - 57 + 3, LanthanideRow};
    }
    if(z <= 86) {
        return {z - 72 + 3, 5};
    }
    if(z <= 88) {
        return {z - 87, 6};
    }
    if(z <= 103) {
        return {z - 89 + 3, LanthanideRow + 1};
    }
    return {z - 104 + 3, 6};
}
}

ElementDialog::ElementDialog(int z, int a, QWidget *parent, bool requireIsotope)
    : QDialog(parent), m_requireIsotope(requireIsotope) {
    setWindowTitle(requireIsotope ? "Select Isotope" : "Select Element");

    auto *table = new QGridLayout;
    table->setSpacing(2);
    table->setRowMinimumHeight(LanthanideRow - 1, 8);
    m_elementGroup = new QButtonGroup(this);
    for(int i = 1; i <= std::min(Elements::maxZ(), 118); i++) {
        auto *button = new QToolButton;
        button->setText(Elements::symbol(i));
        button->setToolTip(QString("Z = %1").arg(i));
        button->setCheckable(true);
        button->setFixedSize(34, 28);
        m_elementGroup->addButton(button, i);
        const QPoint position = tablePosition(i);
        table->addWidget(button, position.y(), position.x());
    }

    m_edit = new QLineEdit;
    m_edit->setPlaceholderText("e.g. Si, 14 or 28Si");
    m_isotopeCombo = new QComboBox;
    auto *form = new QFormLayout;
    form->addRow("Element:", m_edit);
    form->addRow("Isotope:", m_isotopeCombo);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(table);
    layout->addLayout(form);
    layout->addWidget(m_buttonBox);

    connect(m_elementGroup, &QButtonGroup::idClicked, this, &ElementDialog::elementClicked);
    connect(m_edit, &QLineEdit::textEdited, this, &ElementDialog::textEdited);
    connect(m_isotopeCombo, &QComboBox::activated, this, &ElementDialog::isotopeChanged);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setElement(z, a);
    m_edit->setText(z > 0 ? Elements::label(z, a) : QString());
    m_edit->setFocus();
}

int ElementDialog::a() const {
    return m_isotopeCombo->currentData().toInt();
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
