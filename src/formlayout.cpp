#include "formlayout.h"

#include <algorithm>

#include <QHBoxLayout>
#include <QLabel>

QFormLayout *createFormLayout(QWidget *parent) {
    auto *form = new QFormLayout(parent);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    return form;
}

QWidget *buttonField(QWidget *label, QWidget *button) {
    auto *field = new QWidget;
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
