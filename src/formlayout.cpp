/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "formlayout.h"

#include <algorithm>

#include <QAbstractSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>

namespace {
constexpr int MinSpinBoxCharacters = 8; // Narrowest width of spin boxes in forms

void collectSpinBoxes(QLayout *layout, QList<QAbstractSpinBox *> &spinBoxes) {
    for(int i = 0; i < layout->count(); i++) {
        QLayoutItem *item = layout->itemAt(i);
        if(auto *spinBox = qobject_cast<QAbstractSpinBox *>(item->widget())) {
            spinBoxes.append(spinBox);
        } else if(item->layout()) {
            collectSpinBoxes(item->layout(), spinBoxes);
        }
    }
}

// The spin boxes of the forms as wide as the widest of them needs, so that they line up
void setSpinBoxWidths(const QList<QFormLayout *> &forms) {
    QList<QAbstractSpinBox *> spinBoxes;
    for(QFormLayout *form : forms) {
        collectSpinBoxes(form, spinBoxes);
    }
    int width = 0;
    for(QAbstractSpinBox *spinBox : spinBoxes) {
        width = std::max({width, spinBox->sizeHint().width(), spinBox->fontMetrics().averageCharWidth() * MinSpinBoxCharacters});
    }
    for(QAbstractSpinBox *spinBox : spinBoxes) {
        spinBox->setMinimumWidth(width);
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
    QTimer::singleShot(0, form, [form]() { setSpinBoxWidths({form}); });
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

void fitToPlaceholder(QLineEdit *edit) {
    constexpr int ExtraCharacters = 3; // Room for the frame, the margins and a little more
    const QFontMetrics metrics = edit->fontMetrics();
    edit->setFixedWidth(metrics.horizontalAdvance(edit->placeholderText()) + ExtraCharacters * metrics.averageCharWidth());
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
    // After the widths of each form, which are set once its rows have been added
    QTimer::singleShot(0, forms.first(), [forms]() { setSpinBoxWidths(forms); });
}
