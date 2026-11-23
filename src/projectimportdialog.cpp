/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "projectimportdialog.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int ObjectRole = Qt::UserRole;

QString displayName(const QString &name) {
    return name.isEmpty() ? QStringLiteral("(unnamed)") : name;
}
}

ProjectImportDialog::ProjectImportDialog(Project &source, const QString &fileName, const Sample *targetSample, QWidget *parent)
    : QDialog(parent) {
    const bool measurementsOnly = targetSample != nullptr;
    setWindowTitle(QString("Import from %1").arg(QFileInfo(fileName).fileName()));
    resize(450, 500);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    if(!measurementsOnly) {
        if(source.settingsCount() > 0) {
            QTreeWidgetItem *group = addItem(nullptr, "Experimental settings", nullptr);
            for(size_t i = 0; i < source.settingsCount(); i++) {
                addItem(group, displayName(source.settings(i)->name), source.settings(i));
            }
        }
        if(source.detectorCount() > 0) {
            QTreeWidgetItem *group = addItem(nullptr, "Detector settings", nullptr);
            for(size_t i = 0; i < source.detectorCount(); i++) {
                addItem(group, displayName(source.detector(i)->name), source.detector(i));
            }
        }
        if(source.physicsCount() > 0) {
            QTreeWidgetItem *group = addItem(nullptr, "Physics settings", nullptr);
            for(size_t i = 0; i < source.physicsCount(); i++) {
                addItem(group, displayName(source.physics(i)->name), source.physics(i));
            }
        }
    }
    if(source.sampleCount() > 0) {
        QTreeWidgetItem *group = addItem(nullptr, measurementsOnly ? "Measurements" : "Samples", nullptr);
        for(size_t i = 0; i < source.sampleCount(); i++) {
            const Sample *sample = source.sample(i);
            if(measurementsOnly && sample->measurements.empty()) {
                continue;
            }
            QTreeWidgetItem *sampleItem = addItem(group, displayName(sample->name), sample);
            for(const auto &measurement : sample->measurements) {
                addItem(sampleItem, measurement->name, measurement.get());
            }
        }
    }
    m_tree->expandAll();

    auto *label = new QLabel(measurementsOnly ? QString("Measurements to add to sample \"%1\":").arg(displayName(targetSample->name))
                                              : QStringLiteral("Items to import. Samples are added as new samples."));
    label->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(label);
    layout->addWidget(m_tree);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QTreeWidgetItem *ProjectImportDialog::addItem(QTreeWidgetItem *parent, const QString &text, const void *object) {
    auto *item = parent ? new QTreeWidgetItem(parent, {text}) : new QTreeWidgetItem(m_tree, {text});
    // Checking a group or sample checks everything under it, partly checked when only some are
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
    item->setCheckState(0, Qt::Checked);
    item->setData(0, ObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(object)));
    return item;
}

void ProjectImportDialog::accept() {
    m_selected.clear();
    for(QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        const auto *object = reinterpret_cast<const void *>((*it)->data(0, ObjectRole).value<quintptr>());
        if(object && (*it)->checkState(0) != Qt::Unchecked) {
            m_selected.insert(object);
        }
    }
    QDialog::accept();
}
