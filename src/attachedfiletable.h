#ifndef JARA_ATTACHEDFILETABLE_H
#define JARA_ATTACHEDFILETABLE_H

#include <QList>
#include <QStringList>
#include <QTableWidget>

struct AttachedFile;

// The attached files of a sample or of the project with their descriptions, which can be edited. Files are added and
// removed in the project tree.
class AttachedFileTable : public QTableWidget {
    Q_OBJECT

public:
    explicit AttachedFileTable(QList<AttachedFile> *files, QWidget *parent = nullptr); // Not owned
    void refresh(); // Rebuilt only when the files change, not while their descriptions are edited

signals:
    void modified(); // A description was edited

private:
    void itemEdited(QTableWidgetItem *item);

    QList<AttachedFile> *m_files;
    QStringList m_shownPaths;
};

#endif // JARA_ATTACHEDFILETABLE_H
