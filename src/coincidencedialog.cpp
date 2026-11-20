#include "coincidencedialog.h"
#include "defaults.h"
#include "histogramwidget.h"
#include "settingseditors.h"
#include "spectrumplotwidget.h"

#include <algorithm>
#include <cmath>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int PathRole = Qt::UserRole;
enum Column { FileColumn, OutputColumn, SampleColumn };

QString displayName(const QString &name) {
    return name.isEmpty() ? QStringLiteral("(unnamed)") : name;
}

// "erda2_843.evnt" is written to "erda2_843.asc" in the same folder
QString ascPath(const QString &evntPath) {
    const QFileInfo info(evntPath);
    return info.dir().filePath(info.completeBaseName() + QStringLiteral(".asc"));
}

QString count(size_t n) {
    return QLocale().toString(static_cast<qulonglong>(n));
}

// Shows the bytes read of a file, cancelled from the dialog
class FileProgress {
public:
    FileProgress(const QString &label, QWidget *parent) : m_dialog(label, "Cancel", 0, 1000, parent) {
        m_dialog.setWindowModality(Qt::WindowModal);
        m_dialog.setMinimumDuration(500);
    }
    ReadProgress callback() {
        return [this](qint64 done, qint64 total) {
            m_dialog.setValue(static_cast<int>(1000 * done / std::max<qint64>(total, 1)));
            return !m_dialog.wasCanceled();
        };
    }
    bool wasCanceled() const { return m_dialog.wasCanceled(); }

private:
    QProgressDialog m_dialog;
};

// Of the distribution of the time differences, the bins start from the origin
SpectrumPlotWidget::XAxis timeDifferenceAxis(double origin) {
    SpectrumPlotWidget::XAxis axis;
    axis.title = QStringLiteral("Time difference (ticks)");
    axis.unit = QStringLiteral("ticks");
    axis.decimals = 0;
    axis.origin = origin;
    axis.binWidthScale = 1;
    axis.binWidthUnit = QStringLiteral("ticks");
    return axis;
}

// The preview has to be read again when these change
bool sameReading(const CoincidenceSettings &a, const CoincidenceSettings &b) {
    return a.tofAdc == b.tofAdc && a.energyAdc == b.energyAdc && a.reorderSpan == b.reorderSpan;
}
}

CoincidenceDialog::CoincidenceDialog(Project &project, const Sample *currentSample, QWidget *parent)
    : QDialog(parent), m_project(project), m_defaultSample(currentSample ? currentSample->name : QString()) {
    setWindowTitle("Import Event Data");
    resize(1100, 750);
    const CoincidencePreferences preferences = Defaults::coincidencePreferences();
    m_previewSignals = preferences.previewSignals;

    m_files = new QTableWidget(0, 3);
    m_files->setHorizontalHeaderLabels({"Coincidence file", "Writes", "Sample"});
    m_files->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_files->horizontalHeader()->setStretchLastSection(true);
    m_files->verticalHeader()->hide();
    m_files->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_files->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto *addButton = new QPushButton("Add Files...");
    auto *removeButton = new QPushButton("Remove");
    auto *fileButtons = new QHBoxLayout;
    fileButtons->addWidget(addButton);
    fileButtons->addWidget(removeButton);
    fileButtons->addStretch();
    auto *filesGroup = new QGroupBox("Files");
    auto *filesLayout = new QVBoxLayout(filesGroup);
    filesLayout->addWidget(m_files);
    filesLayout->addLayout(fileButtons);

    m_detectorCombo = new QComboBox;
    for(size_t i = 0; i < m_project.detectorCount(); i++) {
        const CoincidenceSettings &settings = m_project.detector(i)->coincidence;
        const QString adcs = settings.hasAdcs() ? QString("ToF ADC %1, E ADC %2").arg(settings.tofAdc).arg(settings.energyAdc)
                                                : QStringLiteral("ADCs not set");
        m_detectorCombo->addItem(QString("%1 (%2)").arg(displayName(m_project.detector(i)->name), adcs), static_cast<int>(i));
        if(settings.hasAdcs() && !detector()->coincidence.hasAdcs()) {
            m_detectorCombo->setCurrentIndex(m_detectorCombo->count() - 1); // The first one with ADCs
        }
    }
    m_detectorNote = new QLabel;
    m_detectorNote->setWordWrap(true);
    m_settingsEditor = new CoincidenceSettingsEditor;
    m_settingsEditor->setAdcsVisible(false); // Edited in the detector settings
    m_marginSpin = new QSpinBox;
    m_marginSpin->setRange(0, 1000000);
    m_marginSpin->setSuffix(" ticks");
    m_marginSpin->setValue(preferences.plotMargin);
    m_marginSpin->setKeyboardTracking(false);
    m_marginSpin->setToolTip("The plot shows this much more on both sides of the window");
    auto *parametersGroup = new QGroupBox("Coincidence parameters");
    auto *parametersLayout = new QVBoxLayout(parametersGroup);
    auto *detectorForm = new QFormLayout;
    detectorForm->setContentsMargins(0, 0, 0, 0);
    detectorForm->addRow("Detector:", m_detectorCombo);
    detectorForm->addRow(m_detectorNote);
    parametersLayout->addLayout(detectorForm);
    parametersLayout->addWidget(m_settingsEditor);
    auto *savedNote = new QLabel("Stored in the detector settings when the files are converted.");
    savedNote->setWordWrap(true);
    parametersLayout->addWidget(savedNote);
    auto *plotForm = new QFormLayout;
    plotForm->setContentsMargins(0, 0, 0, 0);
    plotForm->addRow("Plot shown around the window:", m_marginSpin);

    auto *note = new QLabel("The files are written next to the coincidence files with ToF and E channels and the time "
                            "difference, and added as measurements to their samples. New sample names create samples.");
    note->setWordWrap(true);

    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(filesGroup, 1);
    leftLayout->addWidget(parametersGroup);
    leftLayout->addLayout(plotForm);
    leftLayout->addWidget(note);

    m_histogram = new HistogramWidget;
    m_histogram->setAxes("Time difference", "ticks", "ToF", "ch");
    m_histogram->applyPreferences(Defaults::histogramPreferences());
    m_histogram->setLogScale(true);
    m_histogram->setEvents(&m_plotEvents);
    m_histogram->setMarkersDraggable(true);
    m_statistics = new QLabel;
    m_statistics->setWordWrap(true);
    m_statistics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_distribution = new SpectrumPlotWidget;
    m_distribution->setXAxis(timeDifferenceAxis(0));
    SpectrumPlotWidget::YAxis yAxis;
    yAxis.title = QStringLiteral("Counts");
    yAxis.legendEvents = false;
    m_distribution->setYAxis(yAxis);
    m_distribution->setLogScale(preferences.logScale);
    m_distribution->setAutoYScale(true);
    m_distribution->setToolTip("Drag the edges of the window to change it");
    m_histogram->setToolTip(m_distribution->toolTip());
    auto *plots = new QSplitter(Qt::Vertical);
    plots->addWidget(m_histogram);
    plots->addWidget(m_distribution);
    plots->setSizes({450, 250});
    m_cursorInfo = new QLabel;
    auto *refreshButton = new QPushButton("Refresh Plots");
    refreshButton->setToolTip("Calculate the plots again for the window and the margin around it. They are "
                              "otherwise calculated again only when the window no longer fits in them.");
    auto *cursorRow = new QHBoxLayout;
    cursorRow->addWidget(m_cursorInfo, 1);
    cursorRow->addWidget(refreshButton);
    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(plots, 1);
    rightLayout->addLayout(cursorRow);
    rightLayout->addWidget(m_statistics);

    auto *splitter = new QSplitter;
    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({450, 650});

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttons->button(QDialogButtonBox::Ok)->setText("Convert");
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);
    layout->addWidget(m_buttons);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(300);

    connect(addButton, &QPushButton::clicked, this, &CoincidenceDialog::chooseFiles);
    connect(removeButton, &QPushButton::clicked, this, &CoincidenceDialog::removeSelectedFiles);
    connect(m_files, &QTableWidget::currentCellChanged, this, [this](int row, int, int previousRow) {
        if(row != previousRow) {
            updatePreview();
        }
    });
    connect(m_detectorCombo, &QComboBox::currentIndexChanged, this, &CoincidenceDialog::updateDetector);
    connect(m_settingsEditor, &CoincidenceSettingsEditor::changed, m_previewTimer, qOverload<>(&QTimer::start));
    connect(m_marginSpin, &QSpinBox::valueChanged, m_previewTimer, qOverload<>(&QTimer::start));
    connect(m_previewTimer, &QTimer::timeout, this, [this]() { updatePreview(); });
    connect(refreshButton, &QPushButton::clicked, this, [this]() { updatePreview(true); });
    connect(m_histogram, &HistogramWidget::cursorInfoChanged, m_cursorInfo, &QLabel::setText);
    connect(m_distribution, &SpectrumPlotWidget::cursorInfoChanged, m_cursorInfo, &QLabel::setText);
    connect(m_histogram, &HistogramWidget::markerMoved, this, &CoincidenceDialog::moveWindowEdge);
    connect(m_distribution, &SpectrumPlotWidget::markerMoved, this, &CoincidenceDialog::moveWindowEdge);
    connect(m_distribution, &SpectrumPlotWidget::logScaleChanged, this, [](bool logScale) {
        CoincidencePreferences preferences = Defaults::coincidencePreferences();
        preferences.logScale = logScale;
        Defaults::setCoincidencePreferences(preferences);
    });
    connect(m_buttons, &QDialogButtonBox::accepted, this, &CoincidenceDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateDetector();
}

DetectorSettings *CoincidenceDialog::detector() const {
    const int index = m_detectorCombo->currentData().isValid() ? m_detectorCombo->currentData().toInt() : -1;
    return index >= 0 ? m_project.detector(index) : nullptr;
}

QString CoincidenceDialog::evntPath(int row) const {
    return m_files->item(row, FileColumn)->data(PathRole).toString();
}

void CoincidenceDialog::chooseFiles() {
    const QStringList fileNames = QFileDialog::getOpenFileNames(this, "Add Coincidence Files", projectDirectory(),
                                                                "Coincidence files (*.evnt);;All files (*)");
    addFiles(fileNames);
}

void CoincidenceDialog::addFiles(const QStringList &fileNames) {
    QStringList samples;
    for(size_t i = 0; i < m_project.sampleCount(); i++) {
        samples.append(m_project.sample(i)->name);
    }
    for(const QString &fileName : fileNames) {
        const QString path = QFileInfo(fileName).absoluteFilePath();
        bool listed = false;
        for(int row = 0; row < m_files->rowCount() && !listed; row++) {
            listed = evntPath(row) == path;
        }
        if(listed) {
            continue;
        }
        const int row = m_files->rowCount();
        m_files->insertRow(row);
        auto *file = new QTableWidgetItem(QFileInfo(path).fileName());
        file->setData(PathRole, path);
        file->setToolTip(path);
        m_files->setItem(row, FileColumn, file);
        auto *output = new QTableWidgetItem(QFileInfo(ascPath(path)).fileName());
        output->setToolTip(ascPath(path));
        m_files->setItem(row, OutputColumn, output);
        auto *sample = new QComboBox;
        sample->setEditable(true);
        sample->addItems(samples);
        sample->setCurrentText(m_defaultSample.isEmpty() ? QFileInfo(path).completeBaseName() : m_defaultSample);
        sample->setToolTip("An existing sample, or the name of a new one");
        connect(sample, &QComboBox::currentTextChanged, this, &CoincidenceDialog::updateConvertButton);
        m_files->setCellWidget(row, SampleColumn, sample);
    }
    if(m_files->currentRow() < 0 && m_files->rowCount() > 0) {
        m_files->setCurrentCell(0, FileColumn);
    }
    updateConvertButton();
}

void CoincidenceDialog::removeSelectedFiles() {
    QList<int> rows;
    for(const QModelIndex &index : m_files->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::sort(rows.rbegin(), rows.rend());
    for(int row : rows) {
        m_files->removeRow(row);
    }
    updateConvertButton();
}

void CoincidenceDialog::updateDetector() {
    const DetectorSettings *detector = this->detector();
    m_settings = detector ? detector->coincidence : CoincidenceSettings();
    m_settingsEditor->setSettings(detector ? &m_settings : nullptr);
    m_detectorNote->setText(detector ? "Set the ToF and E ADCs in the detector settings."
                                     : "Add detector settings with the ToF and E ADCs to the project first.");
    m_detectorNote->setVisible(!m_settings.hasAdcs());
    updateConvertButton();
    updatePreview();
}

void CoincidenceDialog::updateConvertButton() {
    bool ready = m_files->rowCount() > 0 && m_settings.hasAdcs();
    for(int row = 0; row < m_files->rowCount() && ready; row++) {
        ready = !static_cast<QComboBox *>(m_files->cellWidget(row, SampleColumn))->currentText().trimmed().isEmpty();
    }
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(ready);
}

void CoincidenceDialog::clearPreview(const QString &message) {
    m_loadedFile.clear();
    m_preview = {};
    m_plotEvents.clear();
    m_hasPlot = false;
    m_histogram->setEvents(&m_plotEvents);
    m_histogram->setVerticalMarkers({});
    m_distribution->setCurves(1, {}, SpectrumPlotWidget::ViewChange::ShowAll);
    m_distribution->setMarkers({});
    m_statistics->setText(message);
}

void CoincidenceDialog::moveWindowEdge(int marker, double x) {
    // The markers are at the edges of the bins of the ends, which are included. The other end is moved along when
    // the window would become empty, as in the editor.
    const auto edge = static_cast<qint64>(std::round(x));
    if(marker == 0) {
        m_settings.windowLow = edge;
        m_settings.windowHigh = std::max(m_settings.windowHigh, edge);
    } else {
        m_settings.windowHigh = edge - 1;
        m_settings.windowLow = std::min(m_settings.windowLow, edge - 1);
    }
    m_settingsEditor->setSettings(&m_settings);
    updatePreview();
}

void CoincidenceDialog::updatePreview(bool recalculatePlots) {
    m_previewTimer->stop();
    const int row = m_files->currentRow();
    if(row < 0) {
        clearPreview("Select a file to see its time differences.");
        return;
    }
    if(!m_settings.hasAdcs()) {
        clearPreview(QString());
        return;
    }
    const QString fileName = evntPath(row);
    if(fileName != m_loadedFile || !sameReading(m_settings, m_loadedWith)) {
        clearPreview(QString());
        QString error;
        FileProgress progress(QString("Reading %1...").arg(QFileInfo(fileName).fileName()), this);
        if(!loadEvntFile(fileName, m_settings, static_cast<size_t>(m_previewSignals), m_preview, error, progress.callback())) {
            clearPreview(QString("Cannot read %1: %2").arg(QFileInfo(fileName).fileName(), error));
            return;
        }
        m_loadedFile = fileName;
        m_loadedWith = m_settings;
    }

    const qint64 margin = m_marginSpin->value();
    // The plot is recalculated only when it no longer covers the window and the margin, so that the zoom is kept
    // while the window is adjusted
    const bool covered = !recalculatePlots && m_hasPlot && m_plotted.triggerIsEnergy == m_settings.triggerIsEnergy
                         && m_plotted.windowLow <= m_settings.windowLow - margin
                         && m_plotted.windowHigh >= m_settings.windowHigh + margin;
    if(!covered) {
        m_plotted = m_settings;
        m_plotted.windowLow -= margin;
        m_plotted.windowHigh += margin;
        m_plotted.pairing = CoincidencePairing::AllCombinations;
        CoincidenceStatistics statistics;
        const std::vector<CoincidenceEvent> pairs = findCoincidences(m_preview.events, m_plotted, statistics);
        m_plotEvents.clear();
        m_plotEvents.reserve(pairs.size());
        for(const CoincidenceEvent &pair : pairs) {
            m_plotEvents.push_back({static_cast<double>(pair.timeDifference), static_cast<double>(pair.tof)});
        }
        m_histogram->setEvents(&m_plotEvents);
        m_histogram->setFullXRange(static_cast<double>(m_plotted.windowLow), static_cast<double>(m_plotted.windowHigh));
        // One tick per bin
        SpectrumPlotWidget::Curve curve{QStringLiteral("All pairs"), QColor(31, 119, 180), pairs.size(),
                                        std::vector<double>(static_cast<size_t>(m_plotted.windowHigh - m_plotted.windowLow + 1))};
        for(const CoincidenceEvent &pair : pairs) {
            curve.values[static_cast<size_t>(pair.timeDifference - m_plotted.windowLow)]++;
        }
        m_distribution->setXAxis(timeDifferenceAxis(static_cast<double>(m_plotted.windowLow)));
        m_distribution->setCurves(1, {curve}, SpectrumPlotWidget::ViewChange::ShowAll);
        m_hasPlot = true;
    }
    // At the edges of the bins of the ends, which are included
    const double low = static_cast<double>(m_settings.windowLow);
    const double high = static_cast<double>(m_settings.windowHigh + 1);
    m_histogram->setVerticalMarkers({low, high});
    m_distribution->setMarkers({{low, Qt::red, Qt::SolidLine, true}, {high, Qt::red, Qt::SolidLine, true}});

    CoincidenceStatistics statistics;
    findCoincidences(m_preview.events, m_settings, statistics);
    const QString trigger = m_settings.triggerIsEnergy ? QStringLiteral("E") : QStringLiteral("ToF");
    const QString partner = m_settings.triggerIsEnergy ? QStringLiteral("ToF") : QStringLiteral("E");
    QString text = m_preview.truncated
                           ? QString("In the first %1 % of the file: ").arg(QLocale().toString(100 * m_preview.fractionRead, 'f', 1))
                           : QString();
    text += QString("%1 %2 and %3 %4 signals, %5 lines ignored")
                    .arg(count(statistics.triggers), trigger, count(statistics.partners), partner, count(m_preview.ignoredLines));
    if(m_preview.outOfOrder > 0) {
        text += QString(", <b>%1 signals out of order</b> (dropped, see the reorder span)").arg(count(m_preview.outOfOrder));
    }
    text += QString(". In the window: %1 pairs, %2 %3 signals with several %4 signals, %5 %4 signals with several %3 "
                    "signals. <b>%6 coincidences</b> with the pairing.")
                    .arg(count(statistics.candidatePairs), count(statistics.ambiguousTriggers), trigger, partner,
                         count(statistics.ambiguousPartners), count(statistics.events));
    m_statistics->setText(text);
}

void CoincidenceDialog::accept() {
    QStringList existing;
    for(int row = 0; row < m_files->rowCount(); row++) {
        if(QFileInfo::exists(ascPath(evntPath(row)))) {
            existing.append(QFileInfo(ascPath(evntPath(row))).fileName());
        }
    }
    if(!existing.isEmpty()) {
        const auto answer = QMessageBox::question(this, "Overwrite Files",
                                                  QString("These files exist already:\n\n%1\n\nOverwrite them?").arg(existing.join("\n")));
        if(answer != QMessageBox::Yes) {
            return;
        }
    }

    QStringList summary;
    QStringList errors;
    m_converted.clear();
    for(int row = 0; row < m_files->rowCount(); row++) {
        const QString fileName = evntPath(row);
        const QString name = QFileInfo(fileName).fileName();
        FileProgress progress(QString("Converting %1 (%2/%3)...").arg(name).arg(row + 1).arg(m_files->rowCount()), this);
        ConversionResult result;
        QString error;
        if(!convertEvntFile(fileName, ascPath(fileName), m_settings, result, error, progress.callback())) {
            if(progress.wasCanceled()) {
                break;
            }
            errors.append(QString("%1: %2").arg(name, error));
            continue;
        }
        m_converted.append({ascPath(fileName), static_cast<QComboBox *>(m_files->cellWidget(row, SampleColumn))->currentText().trimmed()});
        QString line = QString("%1: %2 coincidences").arg(name, count(result.statistics.events));
        if(result.outOfOrder > 0) {
            line += QString(", %1 signals out of order dropped").arg(count(result.outOfOrder));
        }
        summary.append(line);
    }

    if(!errors.isEmpty()) {
        QMessageBox::warning(this, "Import Event Data",
                             QString("%1\n\nNot converted:\n%2").arg(summary.isEmpty() ? QString("Nothing was converted.") : summary.join("\n"),
                                                                     errors.join("\n")));
    }
    if(m_converted.isEmpty()) {
        return;
    }
    detector()->coincidence = m_settings;
    QDialog::accept();
}
