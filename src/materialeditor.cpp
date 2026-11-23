/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "materialeditor.h"
#include "elementdialog.h"
#include "elements.h"

#include <algorithm>
#include <cmath>

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Column { ElementColumn, ConcentrationColumn };
constexpr int Decimals = 6; // Of the concentrations in % when edited

// Edits concentrations in % with a spin box. A value is only stored when it was changed, so that opening and closing
// the editor does not round a concentration that does not fit in the decimals, e.g. after normalizing.
class ConcentrationDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override {
        auto *spinBox = new QDoubleSpinBox(parent);
        spinBox->setDecimals(Decimals);
        spinBox->setRange(0, 100);
        spinBox->setSuffix(" %");
        spinBox->setFrame(false);
        return spinBox;
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        auto *spinBox = static_cast<QDoubleSpinBox *>(editor);
        spinBox->interpretText();
        const double shown = std::round(index.data(Qt::EditRole).toDouble() * std::pow(10, Decimals)) / std::pow(10, Decimals);
        if(spinBox->value() != shown) {
            model->setData(index, spinBox->value(), Qt::EditRole);
        }
    }

    QString displayText(const QVariant &value, const QLocale &locale) const override {
        return locale.toString(value.toDouble(), 'g', 8) + QStringLiteral(" %");
    }
};
}

MaterialEditor::MaterialEditor(QWidget *parent) : QWidget(parent) {
    m_table = new QTableWidget(0, 2);
    m_table->setHorizontalHeaderLabels({"Element", "Concentration (at.%)"});
    m_table->horizontalHeader()->setSectionResizeMode(ElementColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ConcentrationColumn, QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setItemDelegateForColumn(ConcentrationColumn, new ConcentrationDelegate(m_table));
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);

    auto *addButton = new QPushButton("Add Element...");
    m_removeButton = new QPushButton("Remove");
    m_normalizeButton = new QPushButton("Normalize");
    m_normalizeButton->setToolTip("Scale all concentrations so that they sum up to 100 %");
    m_fillButton = new QPushButton("Fill Up to 100 %");
    m_fillButton->setToolTip("Change the concentration of the selected element so that the sum is 100 %, keeping the "
                             "others");
    m_sumLabel = new QLabel;
    m_sumLabel->setWordWrap(true);

    auto *buttons = new QVBoxLayout;
    buttons->addWidget(addButton);
    buttons->addWidget(m_removeButton);
    buttons->addSpacing(12);
    buttons->addWidget(m_normalizeButton);
    buttons->addWidget(m_fillButton);
    buttons->addStretch(1);
    auto *row = new QHBoxLayout;
    row->addWidget(m_table, 1);
    row->addLayout(buttons);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(row);
    layout->addWidget(m_sumLabel);

    connect(addButton, &QPushButton::clicked, this, &MaterialEditor::addElement);
    connect(m_removeButton, &QPushButton::clicked, this, &MaterialEditor::removeElements);
    connect(m_normalizeButton, &QPushButton::clicked, this, &MaterialEditor::normalize);
    connect(m_fillButton, &QPushButton::clicked, this, &MaterialEditor::fillUp);
    connect(m_table, &QTableWidget::itemChanged, this, &MaterialEditor::itemChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &MaterialEditor::updateState);
    updateTable();
}

void MaterialEditor::setMaterial(const Material &material) {
    m_material = material;
    sortElements(m_material);
    updateTable();
}

void MaterialEditor::updateTable() {
    const QSignalBlocker blocker(m_table);
    m_table->setRowCount(static_cast<int>(m_material.elements.size()));
    for(int row = 0; row < m_material.elements.size(); row++) {
        const MaterialElement &element = m_material.elements[row];
        auto *elementItem = new QTableWidgetItem(Elements::symbol(element.z));
        elementItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, ElementColumn, elementItem);
        auto *concentrationItem = new QTableWidgetItem;
        concentrationItem->setData(Qt::EditRole, element.fraction * 100);
        concentrationItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, ConcentrationColumn, concentrationItem);
    }
    updateState();
}

double MaterialEditor::sum() const {
    double sum = 0;
    for(const MaterialElement &element : m_material.elements) {
        sum += element.fraction;
    }
    return sum;
}

int MaterialEditor::selectedRow() const {
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    return rows.size() == 1 ? rows.first().row() : -1;
}

void MaterialEditor::updateState() {
    const double total = sum();
    const bool complete = std::abs(total - 1) <= MaterialFractionTolerance;
    if(m_material.elements.isEmpty()) {
        m_sumLabel->setText("Add the elements of the material.");
        m_sumLabel->setStyleSheet("color: red");
    } else if(complete) {
        m_sumLabel->setText("Sum 100 %");
        m_sumLabel->setStyleSheet({});
    } else {
        m_sumLabel->setText(QString("Sum %1 %. Normalize the concentrations or fill up one element to 100 %.")
                                    .arg(total * 100, 0, 'g', 8));
        m_sumLabel->setStyleSheet("color: red");
    }
    const int row = selectedRow();
    m_removeButton->setEnabled(m_table->selectionModel()->hasSelection());
    m_normalizeButton->setEnabled(total > 0 && !complete);
    // The others must leave room for the selected element
    m_fillButton->setEnabled(row >= 0 && !complete && total - m_material.elements[row].fraction <= 1 + MaterialFractionTolerance);
}

void MaterialEditor::itemChanged(QTableWidgetItem *item) {
    if(item->column() != ConcentrationColumn) {
        return;
    }
    m_material.elements[item->row()].fraction = std::clamp(item->data(Qt::EditRole).toDouble() / 100, 0.0, 1.0);
    updateState();
    emit changed();
}

void MaterialEditor::addElement() {
    ElementDialog dialog(0, 0, this);
    if(dialog.exec() != QDialog::Accepted || dialog.z() <= 0) {
        return;
    }
    // The natural isotopic composition is used, an isotope chosen in the dialog is not
    const int z = dialog.z();
    for(int row = 0; row < m_material.elements.size(); row++) {
        if(m_material.elements[row].z == z) {
            m_table->selectRow(row);
            return;
        }
    }
    m_material.elements.append({z, std::max(0.0, 1 - sum())}); // What is missing from 100 %
    sortElements(m_material);
    updateTable();
    for(int row = 0; row < m_material.elements.size(); row++) {
        if(m_material.elements[row].z == z) {
            m_table->selectRow(row);
        }
    }
    emit changed();
}

void MaterialEditor::removeElements() {
    QList<int> rows;
    for(const QModelIndex &index : m_table->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<>());
    for(const int row : rows) {
        m_material.elements.removeAt(row);
    }
    updateTable();
    emit changed();
}

void MaterialEditor::normalize() {
    const double total = sum();
    if(total <= 0) {
        return;
    }
    for(MaterialElement &element : m_material.elements) {
        element.fraction /= total;
    }
    updateTable();
    emit changed();
}

void MaterialEditor::fillUp() {
    const int row = selectedRow();
    if(row < 0) {
        return;
    }
    MaterialElement &element = m_material.elements[row];
    element.fraction = std::max(0.0, 1 - (sum() - element.fraction));
    updateTable();
    m_table->selectRow(row);
    emit changed();
}
