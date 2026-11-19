#include "tableexport.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSaveFile>

namespace {
const QString TextFilter = QStringLiteral("Text files (*.txt)");
const QString CsvFilter = QStringLiteral("CSV files (*.csv)");

// Quoted when it has a comma, a quote or a line break, quotes doubled
QString csvField(const QString &field) {
    if(!field.contains(QLatin1Char(',')) && !field.contains(QLatin1Char('"')) && !field.contains(QLatin1Char('\n'))) {
        return field;
    }
    QString quoted = field;
    quoted.replace(QLatin1String("\""), QLatin1String("\"\""));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QString csvRow(const QStringList &fields) {
    QStringList quoted;
    for(const QString &field : fields) {
        quoted.append(csvField(field));
    }
    return quoted.join(QLatin1Char(',')) + QLatin1Char('\n');
}
}

QString exportTable(QWidget *parent, const QString &title, const QString &baseName, const ExportTable &table) {
    QString filter = TextFilter;
    QString fileName = QFileDialog::getSaveFileName(parent, title, baseName + QStringLiteral(".txt"),
                                                    TextFilter + QStringLiteral(";;") + CsvFilter, &filter);
    if(fileName.isEmpty()) {
        return {};
    }
    if(QFileInfo(fileName).suffix().isEmpty()) {
        fileName += filter == CsvFilter ? QStringLiteral(".csv") : QStringLiteral(".txt");
    }
    const bool csv = QFileInfo(fileName).suffix().compare(QLatin1String("csv"), Qt::CaseInsensitive) == 0;
    QString text;
    if(csv) {
        text += csvRow(table.columns);
        for(const QStringList &row : table.rows) {
            text += csvRow(row);
        }
    } else {
        for(const QString &comment : table.comments) {
            text += QStringLiteral("# ") + comment + QLatin1Char('\n');
        }
        text += QStringLiteral("# ") + table.columns.join(QLatin1Char('\t')) + QLatin1Char('\n');
        for(const QStringList &row : table.rows) {
            text += row.join(QLatin1Char('\t')) + QLatin1Char('\n');
        }
    }
    QSaveFile file(fileName);
    if(!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        QMessageBox::critical(parent, title, QString("Could not write %1:\n%2").arg(fileName, file.errorString()));
        return {};
    }
    return fileName;
}
