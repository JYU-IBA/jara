/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_MATERIALEDITOR_H
#define JARA_MATERIALEDITOR_H

#include <QWidget>

#include "project.h"

class QLabel;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

// Edits the elements of a material and their concentrations in atomic-%. The user makes the concentrations sum up to
// 100 %, either by normalizing them or by filling one element up to 100 % while the others are kept.
class MaterialEditor : public QWidget {
    Q_OBJECT

public:
    explicit MaterialEditor(QWidget *parent = nullptr);
    void setMaterial(const Material &material);
    const Material &material() const { return m_material; }
    bool isValid() const { return isValidMaterial(m_material); }

signals:
    void changed();

private slots:
    void addElement();
    void removeElements();
    void normalize();
    void fillUp();
    void itemChanged(QTableWidgetItem *item);

private:
    void updateTable();
    void updateState(); // The sum and the buttons
    double sum() const; // Of the fractions
    int selectedRow() const; // -1 unless exactly one row is selected

    Material m_material; // Concentrations as edited, the table shows them rounded
    QTableWidget *m_table;
    QLabel *m_sumLabel;
    QPushButton *m_removeButton;
    QPushButton *m_normalizeButton;
    QPushButton *m_fillButton;
};

#endif // JARA_MATERIALEDITOR_H
