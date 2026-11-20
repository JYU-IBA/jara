#include "formlayout.h"

#include <algorithm>

#include <QAbstractSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>

namespace {
constexpr int SpinBoxCharacters = 20; // Minimum width of spin boxes, wider than most need

void setSpinBoxWidths(QLayout *layout) {
    for(int i = 0; i < layout->count(); i++) {
        QLayoutItem *item = layout->itemAt(i);
        if(auto *spinBox = qobject_cast<QAbstractSpinBox *>(item->widget())) {
            spinBox->setMinimumWidth(std::max(spinBox->minimumSizeHint().width(),
                                              spinBox->fontMetrics().averageCharWidth() * SpinBoxCharacters));
        } else if(item->layout()) {
            setSpinBoxWidths(item->layout());
        }
    }
}
}

QFormLayout *createFormLayout(QWidget *parent) {
    auto *form = new QFormLayout(parent);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    // Once the rows have been added, before the form is shown
    QTimer::singleShot(0, form, [form]() { setSpinBoxWidths(form); });
    return form;
}

QWidget *buttonField(QWidget *label, QWidget *button) {
    auto *field = new QWidget;
    field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); // The button at the right edge of the form
    auto *layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    if(label) {
        layout->addWidget(label, 1);
    } else {
        layout->addStretch(1);
    }
    layout->addWidget(button);
    return field;
}

void alignFormLabels(const QList<QFormLayout *> &forms) {
    QList<QWidget *> labels;
    int width = 0;
    for(QFormLayout *form : forms) {
        for(int row = 0; row < form->rowCount(); row++) {
            if(QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole); item && item->widget()) {
                labels.append(item->widget());
                width = std::max(width, item->widget()->sizeHint().width());
            }
        }
    }
    for(QWidget *label : labels) {
        label->setMinimumWidth(width);
    }
}
