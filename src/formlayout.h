#ifndef JARA_FORMLAYOUT_H
#define JARA_FORMLAYOUT_H

#include <QFormLayout>
#include <QList>

// Form layouts look the same on all platforms: labels left-aligned, the form at the top left and the fields growing
// to its width. The default of macOS (right-aligned labels, a centered form, fields at their natural widths) gives
// ragged forms where rows with buttons and rows with spin boxes end at different places.
QFormLayout *createFormLayout(QWidget *parent = nullptr);

// A field of a text and a button at its right, e.g. a summary and Edit..., or of a button alone at the right (label
// nullptr). In a widget of their own, so that a wrapped text gets its height.
QWidget *buttonField(QWidget *label, QWidget *button);

// Forms shown together, e.g. in group boxes one after another, get labels of the same width, so that their fields
// start at the same place. Call after the rows are added.
void alignFormLabels(const QList<QFormLayout *> &forms);

#endif // JARA_FORMLAYOUT_H
