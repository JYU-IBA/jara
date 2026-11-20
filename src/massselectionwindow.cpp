#include "massselectionwindow.h"
#include "colorbutton.h"
#include "defaults.h"
#include "elementdialog.h"
#include "elements.h"
#include "histogramwidget.h"

#include <cmath>

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QThread>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int BandRole = Qt::UserRole;
const QColor RidgeColor(0x60, 0x60, 0x60);

enum Column { BandColumn, BasisColumn, EdgeColumn, ExpectedColumn, DeviationColumn, RangeColumn, EventsColumn,
              OthersColumn, EndsColumn };

QString basisText(IdentityBasis basis) {
    switch(basis) {
    case IdentityBasis::Edge:
        return QStringLiteral("edge");
    case IdentityBasis::Response:
        return QStringLiteral("response");
    case IdentityBasis::User:
        return QStringLiteral("you");
    case IdentityBasis::None:
        break;
    }
    return QStringLiteral("—");
}

QString identityText(const BandIdentity &identity) {
    return identity.isKnown() ? cutLabel(identity.z, identity.a, identity.type) : QStringLiteral("Unknown");
}

QString channelText(double channel) {
    return std::isfinite(channel) ? QString::number(channel, 'f', 0) : QStringLiteral("—");
}

// Finds the bands, identities and selections, in a thread of its own. Identities given by the user are kept.
template<typename Analysis> void analyze(Analysis &analysis, const std::vector<ToFEEvent> &events) {
    if(analysis.findBands) {
        analysis.bands = findMassBands(events, analysis.detector.tofSlope, analysis.detector.tofOffset);
    }
    if(analysis.bands.empty()) {
        analysis.errorMessage = QStringLiteral("No mass bands were found.");
        return;
    }
    EnergyDetectorModel model(analysis.detector, analysis.physics);
    if(analysis.propose) {
        std::vector<BandIdentity> given(analysis.bands.size());
        for(size_t b = 0; b < analysis.proposals.size() && b < given.size(); b++) {
            if(analysis.proposals[b].basis == IdentityBasis::User) {
                given[b] = analysis.proposals[b].identity;
            }
        }
        IdentityProposal proposal = proposeIdentities(analysis.bands, analysis.settings, analysis.detector, model,
                                                      analysis.physics, given);
        analysis.proposals = std::move(proposal.bands);
        analysis.calibration = proposal.calibration;
    } else {
        std::vector<BandIdentity> identities;
        for(const BandProposal &proposal : analysis.proposals) {
            identities.push_back(proposal.identity);
        }
        analysis.calibration = calibrateFromBands(analysis.bands, identities, analysis.settings, model);
        if(!analysis.calibration.ok && analysis.detector.energyCalibration.valid) {
            analysis.calibration.calibration = analysis.detector.energyCalibration;
            analysis.calibration.rmsResidual = std::numeric_limits<double>::quiet_NaN();
            analysis.calibration.ok = true;
        }
        for(size_t b = 0; b < analysis.proposals.size(); b++) {
            analysis.proposals[b].deviation = analysis.calibration.ok
                                                      ? bandDeviation(analysis.bands[b], analysis.proposals[b].identity,
                                                                      analysis.settings, model, analysis.calibration.calibration)
                                                      : std::numeric_limits<double>::quiet_NaN();
        }
    }
    std::vector<SelectionBand> bands;
    std::vector<size_t> indices;
    for(size_t b = 0; b < analysis.bands.size(); b++) {
        const BandProposal &proposal = analysis.proposals[b];
        if(proposal.continues < 0 && !proposal.artifact) {
            bands.push_back({&analysis.bands[b], detectedZ(proposal.identity, &analysis.settings),
                             detectedMass(proposal.identity, &analysis.settings), true});
            indices.push_back(b);
        }
    }
    std::vector<BandSelection> selections = selectBands(events, bands, model, analysis.calibration.calibration,
                                                        analysis.options);
    analysis.selections.assign(analysis.bands.size(), {});
    for(size_t i = 0; i < indices.size(); i++) {
        analysis.selections[indices[i]] = std::move(selections[i]);
    }
}
}

MassSelectionWindow::MassSelectionWindow(Measurement *measurement, const std::vector<ToFEEvent> *events, QWidget *parent)
    : QWidget(parent, Qt::Window), m_measurement(measurement), m_events(events) {
    setWindowTitle(QString("Automatic Selections — %1").arg(measurement->name));

    m_table = new QTreeWidget;
    m_table->setRootIsDecorated(false);
    m_table->setHeaderLabels({"Band", "By", "Edge (ch)", "Expected (ch)", "Deviation (%)", "Selection (ch)", "Events",
                              "Others (%)", "Ends because"});
    m_table->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->headerItem()->setToolTip(BandColumn, "Check the bands to select. Double-click to identify a band.");
    m_table->headerItem()->setToolTip(BasisColumn, "How the band was identified: by its leading edge, by how its ridge "
                                                   "follows the response of the energy detector, or by you");
    m_table->headerItem()->setToolTip(EdgeColumn, "Leading (short ToF) edge of the band");
    m_table->headerItem()->setToolTip(ExpectedColumn, "Where the particles from the surface are expected");
    m_table->headerItem()->setToolTip(DeviationColumn, "Of the ridge of the band from the response of the energy detector "
                                                       "to its particles, root mean square");
    m_table->headerItem()->setToolTip(EventsColumn, "Expected events of the band in its selection");
    m_table->headerItem()->setToolTip(OthersColumn, "Expected events of other bands and the background in the selection");

    m_overlapSpin = new QDoubleSpinBox;
    m_overlapSpin->setRange(0.1, 20);
    m_overlapSpin->setDecimals(1);
    m_overlapSpin->setSingleStep(0.5);
    m_overlapSpin->setValue(100 * SelectionOptions().maxContamination);
    m_overlapSpin->setSuffix(" %");
    m_overlapSpin->setKeyboardTracking(false);
    m_overlapSpin->setToolTip("A selection ends where events of other bands would exceed this fraction of its events");
    m_proposeButton = new QPushButton("Propose Again");
    m_proposeButton->setToolTip("Identify the bands again, your identities are kept");
    m_calibrationLabel = new QLabel;
    m_calibrationLabel->setWordWrap(true);
    m_applyButton = new QPushButton("Apply to Detector");
    m_applyButton->setToolTip("Store the calibration of the energy detector in the detector settings, it is then used "
                              "when too few bands can be identified by their edges, also in other measurements");
    m_makeButton = new QPushButton("Make Selections");
    m_makeButton->setToolTip("Make selections of the checked bands. Earlier automatic selections are replaced, those of "
                             "the same particle keep their place in spectra and depth profiles.");
    m_statusLabel = new QLabel;
    m_statusLabel->setWordWrap(true);

    auto *overlapRow = new QHBoxLayout;
    overlapRow->addWidget(new QLabel("Overlap with other bands at most:"));
    overlapRow->addWidget(m_overlapSpin);
    overlapRow->addStretch(1);
    overlapRow->addWidget(m_proposeButton);
    auto *calibrationRow = new QHBoxLayout;
    calibrationRow->addWidget(m_calibrationLabel, 1);
    calibrationRow->addWidget(m_applyButton);
    auto *makeRow = new QHBoxLayout;
    makeRow->addWidget(m_statusLabel, 1);
    makeRow->addWidget(m_makeButton);
    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(m_table, 1);
    leftLayout->addLayout(overlapRow);
    leftLayout->addLayout(calibrationRow);
    leftLayout->addLayout(makeRow);

    m_histogram = new HistogramWidget;
    m_histogram->applyPreferences(Defaults::histogramPreferences());
    m_histogram->setLogScale(true);
    m_histogram->setEvents(events);
    m_histogram->setCuts(&m_previewCuts);
    auto *toolBar = new QToolBar;
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolBar->addAction("Zoom In", m_histogram, &HistogramWidget::zoomIn);
    toolBar->addAction("Zoom Out", m_histogram, &HistogramWidget::zoomOut);
    toolBar->addAction("Show All", m_histogram, &HistogramWidget::resetZoom);
    m_cursorInfo = new QLabel;
    m_cursorInfo->setMinimumHeight(m_cursorInfo->fontMetrics().height());
    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(toolBar);
    rightLayout->addWidget(m_histogram, 1);
    rightLayout->addWidget(m_cursorInfo);

    auto *splitter = new QSplitter;
    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({620, 700});
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);
    resize(1350, 800);

    connect(m_overlapSpin, &QDoubleSpinBox::valueChanged, this, [this]() { start(false, false); });
    connect(m_proposeButton, &QPushButton::clicked, this, [this]() { start(false, true); });
    connect(m_applyButton, &QPushButton::clicked, this, &MassSelectionWindow::applyCalibration);
    connect(m_makeButton, &QPushButton::clicked, this, &MassSelectionWindow::makeSelections);
    connect(m_table, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) { identify(item); });
    connect(m_table, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        const int band = bandOf(item);
        if(column == BandColumn && band >= 0) {
            m_checked[static_cast<size_t>(band)] = item->checkState(BandColumn) == Qt::Checked;
            updateOverlay();
        }
    });
    connect(m_table, &QTreeWidget::currentItemChanged, this, &MassSelectionWindow::updateOverlay);
    connect(m_table, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_table->itemAt(pos);
        if(!item) {
            return;
        }
        QMenu menu;
        menu.addAction("Identify...", this, [this, item]() { identify(item); });
        menu.addAction("Clear Identity", this, [this, item]() { clearIdentity(item); })
                ->setEnabled(m_analysis.proposals[static_cast<size_t>(bandOf(item))].identity.isKnown());
        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });
    connect(m_histogram, &HistogramWidget::cursorInfoChanged, m_cursorInfo, &QLabel::setText);

    showAnalysis();
    start(true, true);
}

MassSelectionWindow::~MassSelectionWindow() {
    // The calculation refers to the events
    if(m_thread) {
        m_thread->wait();
    }
}

void MassSelectionWindow::start(bool findBands, bool propose) {
    if(m_thread) {
        m_restart = true;
        m_restartFind = m_restartFind || findBands;
        m_restartPropose = m_restartPropose || propose;
        return;
    }
    const DetectorSettings *detector = m_measurement->detector;
    if(!detector || detector->tofSlope <= 0 || detector->tofLength <= 0) {
        m_statusLabel->setText("The measurement needs detector settings with the ToF calibration.");
        return;
    }
    findBands = findBands || m_analysis.bands.empty();
    propose = propose || findBands;
    auto analysis = std::make_shared<Analysis>();
    analysis->findBands = findBands;
    analysis->propose = propose;
    if(m_measurement->settings) {
        analysis->settings = *m_measurement->settings;
    }
    analysis->detector = *detector;
    analysis->physics = m_measurement->physicsOrDefault();
    analysis->options.maxContamination = m_overlapSpin->value() / 100;
    // The identities are kept with the bands, also when proposing the others again
    if(!findBands) {
        analysis->bands = m_analysis.bands;
        analysis->proposals = m_analysis.proposals;
    }
    m_running = analysis;
    m_thread = QThread::create([analysis, events = m_events]() { analyze(*analysis, *events); });
    connect(m_thread, &QThread::finished, this, &MassSelectionWindow::finished);
    m_thread->start();
    m_statusLabel->setText(findBands ? "Finding the mass bands..." : "Calculating the selections...");
    for(QPushButton *button : {m_proposeButton, m_applyButton, m_makeButton}) {
        button->setEnabled(false);
    }
}

void MassSelectionWindow::finished() {
    m_thread->deleteLater();
    m_thread = nullptr;
    const bool newBands = m_running->findBands;
    m_analysis = std::move(*m_running);
    m_running.reset();
    // New bands are checked when they are identified and can be selected, otherwise the choices are kept
    if(newBands || m_checked.size() != m_analysis.bands.size()) {
        m_checked.assign(m_analysis.bands.size(), false);
        for(size_t b = 0; b < m_analysis.bands.size(); b++) {
            m_checked[b] = m_analysis.proposals[b].identity.isKnown() && m_analysis.selections[b].ok;
        }
    }
    showAnalysis();
    if(m_restart) {
        m_restart = false;
        const bool findBands = m_restartFind;
        const bool propose = m_restartPropose;
        m_restartFind = false;
        m_restartPropose = false;
        start(findBands, propose);
    }
}

int MassSelectionWindow::bandOf(const QTreeWidgetItem *item) const {
    return item ? item->data(BandColumn, BandRole).toInt() : -1;
}

void MassSelectionWindow::showAnalysis() {
    const QSignalBlocker blocker(m_table);
    const int current = bandOf(m_table->currentItem());
    m_table->clear();
    const Analysis &analysis = m_analysis;
    const DetectorSettings *detector = m_measurement->detector;
    for(size_t b = 0; b < analysis.bands.size(); b++) {
        const BandProposal &proposal = analysis.proposals[b];
        const MassBand &band = analysis.bands[b];
        const BandSelection &selection = analysis.selections[b];
        // Pieces of other bands, and deep fragments without identity or selection, are left out
        if(proposal.continues >= 0 || proposal.artifact || (!proposal.identity.isKnown() && !selection.ok)) {
            continue;
        }
        auto *item = new QTreeWidgetItem(m_table);
        item->setData(BandColumn, BandRole, static_cast<int>(b));
        item->setText(BandColumn, identityText(proposal.identity));
        if(proposal.identity.isKnown()) {
            item->setIcon(BandColumn, colorSwatch(Defaults::elementColor(proposal.identity.z)));
        }
        if(proposal.identity.type == CutType::Scattered && proposal.identity.isKnown()) {
            item->setToolTip(BandColumn, QString("Incident ions scattered from %1").arg(Elements::displayLabel(proposal.identity.z,
                                                                                                             proposal.identity.a)));
        }
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if(proposal.identity.isKnown() && selection.ok) {
            flags |= Qt::ItemIsUserCheckable;
            item->setCheckState(BandColumn, m_checked[b] ? Qt::Checked : Qt::Unchecked);
        }
        item->setFlags(flags);
        item->setText(BasisColumn, basisText(proposal.basis));
        item->setText(EdgeColumn, channelText(band.edgeTof));
        item->setText(ExpectedColumn, channelText(proposal.expectedEdge));
        item->setText(DeviationColumn, std::isfinite(proposal.deviation) ? QString::number(100 * proposal.deviation, 'f', 1)
                                                                         : QStringLiteral("—"));
        if(selection.ok) {
            item->setText(RangeColumn, QString("%1–%2").arg(selection.startTof, 0, 'f', 0).arg(selection.endTof, 0, 'f', 0));
            item->setText(EventsColumn, QString::number(selection.events, 'f', 0));
            item->setText(OthersColumn, QString::number(100 * selection.contamination, 'f', 1));
        }
        item->setText(EndsColumn, selection.endReason);
        item->setToolTip(EndsColumn, selection.endReason);
        if(static_cast<int>(b) == current) {
            m_table->setCurrentItem(item);
        }
    }

    // The calibration and what there is to do
    const EnergyDetectorFit &fit = analysis.calibration;
    if(fit.ok) {
        QString text = energyCalibrationText(fit.calibration);
        text += std::isfinite(fit.rmsResidual) ? QString(". Fitted to %1 particles, within %2 % (rms).").arg(fit.particles)
                                                         .arg(100 * fit.rmsResidual, 0, 'f', 1)
                                               : QStringLiteral(", from the detector settings.");
        m_calibrationLabel->setText(text);
    } else {
        m_calibrationLabel->setText(QString("No energy detector calibration: %1").arg(fit.errorMessage));
    }
    if(!m_thread) {
        m_proposeButton->setEnabled(!analysis.bands.empty());
        m_applyButton->setEnabled(fit.ok && std::isfinite(fit.rmsResidual) && detector);
        m_makeButton->setEnabled(std::any_of(m_checked.begin(), m_checked.end(), [](bool checked) { return checked; }));
        m_statusLabel->setText(analysis.errorMessage.isEmpty()
                                       ? QString("%1 bands. Double-click a band to identify it.").arg(m_table->topLevelItemCount())
                                       : analysis.errorMessage);
    }
    updateOverlay();
}

QList<Cut> MassSelectionWindow::proposedCuts(bool all) const {
    QList<Cut> cuts;
    const Analysis &analysis = m_analysis;
    for(size_t b = 0; b < analysis.bands.size(); b++) {
        const BandIdentity &identity = analysis.proposals[b].identity;
        if(analysis.proposals[b].continues >= 0 || analysis.proposals[b].artifact || !identity.isKnown()
           || !analysis.selections[b].ok || (!all && !m_checked[b])) {
            continue;
        }
        // Isotopes are selected separately when another isotope of the element has a band of its own
        bool resolved = false;
        for(size_t other = 0; other < analysis.bands.size(); other++) {
            const BandIdentity &otherIdentity = analysis.proposals[other].identity;
            if(other != b && analysis.proposals[other].continues < 0 && !analysis.proposals[other].artifact && otherIdentity.z == identity.z
               && otherIdentity.type == identity.type && otherIdentity.a != identity.a) {
                resolved = true;
            }
        }
        Cut cut;
        cut.z = identity.z;
        cut.a = resolved && identity.type == CutType::Recoil ? identity.a : 0;
        cut.type = identity.type;
        cut.polygon = analysis.selections[b].polygon;
        cut.color = Defaults::elementColor(identity.z);
        cut.automatic = true;
        cut.id = static_cast<int>(b); // Only for the preview, the band it belongs to
        cuts.append(cut);
    }
    return cuts;
}

void MassSelectionWindow::updateOverlay() {
    m_previewCuts = proposedCuts(false);
    const int current = bandOf(m_table->currentItem());
    int highlighted = -1;
    for(qsizetype i = 0; i < m_previewCuts.size(); i++) {
        if(m_previewCuts[i].id == current) {
            highlighted = static_cast<int>(i);
        }
    }
    m_histogram->setHighlightedCut(highlighted);
    // Ridges of the bands, and the selections of the unchecked ones dashed
    QList<HistogramWidget::OverlayLine> lines;
    const DetectorSettings *detector = m_measurement->detector;
    for(size_t b = 0; detector && b < m_analysis.bands.size(); b++) {
        QPolygonF ridge;
        for(const QPointF &point : m_analysis.bands[b].ridge) {
            const double t = (detector->tofSlope * point.x() + detector->tofOffset) / Units::Nanosecond;
            if(t > 0) {
                ridge.append({point.x(), std::exp(point.y()) / (t * t)});
            }
        }
        lines.append({ridge, RidgeColor, Qt::DotLine, static_cast<int>(b) == current ? 2.5 : 1});
        const BandSelection &selection = m_analysis.selections[b];
        if(selection.ok && !m_checked[b]) {
            QPolygonF outline(selection.polygon);
            outline.append(selection.polygon.front());
            const QColor color = m_analysis.proposals[b].identity.isKnown() ? Defaults::elementColor(m_analysis.proposals[b].identity.z)
                                                                            : RidgeColor;
            lines.append({outline, color, Qt::DashLine, static_cast<int>(b) == current ? 2.5 : 1.5});
        }
    }
    m_histogram->setOverlayLines(lines);
    m_histogram->update();
}

void MassSelectionWindow::identify(QTreeWidgetItem *item) {
    const int band = bandOf(item);
    if(band < 0 || m_thread) {
        return;
    }
    BandProposal &proposal = m_analysis.proposals[static_cast<size_t>(band)];
    ElementDialog dialog(proposal.identity.z, proposal.identity.a, this, true);
    dialog.setWindowTitle("Identify Band");
    dialog.setTypeSelectable(proposal.identity.type);
    if(dialog.exec() != QDialog::Accepted || dialog.type() == CutType::Reference || dialog.z() <= 0) {
        return;
    }
    proposal.identity = {dialog.z(), dialog.a(), dialog.type()};
    proposal.basis = IdentityBasis::User;
    proposal.expectedEdge = std::numeric_limits<double>::quiet_NaN();
    m_checked[static_cast<size_t>(band)] = true;
    start(false, false);
}

void MassSelectionWindow::clearIdentity(QTreeWidgetItem *item) {
    const int band = bandOf(item);
    if(band < 0 || m_thread) {
        return;
    }
    BandProposal &proposal = m_analysis.proposals[static_cast<size_t>(band)];
    proposal.identity = {};
    proposal.basis = IdentityBasis::None;
    proposal.expectedEdge = std::numeric_limits<double>::quiet_NaN();
    m_checked[static_cast<size_t>(band)] = false;
    start(false, false);
}

void MassSelectionWindow::applyCalibration() {
    DetectorSettings *detector = m_measurement->detector;
    if(!detector || !m_analysis.calibration.ok) {
        return;
    }
    detector->energyCalibration = m_analysis.calibration.calibration;
    m_statusLabel->setText(QString("The energy detector calibration was stored in the detector settings %1.").arg(detector->name));
    emit calibrationApplied(detector);
}

void MassSelectionWindow::makeSelections() {
    QList<Cut> cuts = proposedCuts(false);
    QList<Cut> &existing = m_measurement->cuts;
    // Automatic selections of the same particle are updated, the others are made anew
    std::vector<bool> updated(static_cast<size_t>(existing.size()), false);
    QList<Cut> added;
    for(Cut &cut : cuts) {
        bool found = false;
        for(qsizetype i = 0; i < existing.size() && !found; i++) {
            Cut &old = existing[i];
            if(old.automatic && !updated[static_cast<size_t>(i)] && old.z == cut.z && old.a == cut.a && old.type == cut.type) {
                old.polygon = cut.polygon;
                updated[static_cast<size_t>(i)] = true;
                found = true;
            }
        }
        if(!found) {
            added.append(cut);
        }
    }
    QList<int> stale; // Earlier automatic selections not made again
    for(qsizetype i = 0; i < existing.size(); i++) {
        if(existing[i].automatic && !updated[static_cast<size_t>(i)]) {
            stale.append(static_cast<int>(i));
        }
    }
    bool removeStale = false;
    if(!stale.isEmpty()) {
        QStringList names;
        for(const int i : stale) {
            names.append(cutLabel(existing[i]));
        }
        removeStale = QMessageBox::question(this, "Make Selections",
                                            QString("Remove the earlier automatic selections that were not made again "
                                                    "(%1)? They are also removed from spectra and depth profiles.")
                                                    .arg(names.join(", ")))
                      == QMessageBox::Yes;
    }
    if(removeStale) {
        for(auto it = stale.rbegin(); it != stale.rend(); ++it) {
            m_measurement->removeCut(*it);
        }
    }
    for(Cut cut : added) {
        cut.id = m_measurement->newCutId();
        existing.append(cut);
    }
    const qsizetype updatedCount = std::count(updated.begin(), updated.end(), true);
    m_statusLabel->setText(QString("Made %1 selections: %2 updated, %3 new%4.")
                                   .arg(cuts.size())
                                   .arg(updatedCount)
                                   .arg(added.size())
                                   .arg(removeStale ? QString(", %1 removed").arg(stale.size()) : QString()));
    emit selectionsMade();
}
