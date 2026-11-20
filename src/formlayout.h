#ifndef JARA_FORMLAYOUT_H
#define JARA_FORMLAYOUT_H

#include <QFormLayout>
#include <QLineEdit>
#include <QList>

// Form layouts look the same on all platforms: labels left-aligned, the form at the top left, spin boxes and combo
// boxes at their natural widths and expanding fields (line edits, text with a button) growing to the width of the
// form. The default of macOS right-aligns the labels and centres the form. The spin boxes of the form, also those in
// rows of their own layouts, are made as wide as the widest of them needs, once the form has been built (the rows are
// added after this).
QFormLayout *createFormLayout(QWidget *parent = nullptr);

// A field of a text and a button at its right, e.g. a summary and Edit..., or of a button alone at the right (label
// nullptr). In a widget of their own, so that a wrapped text gets its height.
QWidget *buttonField(QWidget *label, QWidget *button);

// A line edit for short values, e.g. an ion "63Cu", as wide as its placeholder text instead of growing in a form
void fitToPlaceholder(QLineEdit *edit);

// Forms shown together, e.g. in group boxes one after another, get labels of the same width, so that their fields
// start at the same place, and spin boxes of the same width. Call after the rows are added.
void alignFormLabels(const QList<QFormLayout *> &forms);

#endif // JARA_FORMLAYOUT_H
