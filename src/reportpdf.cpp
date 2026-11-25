/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "reportpdf.h"
#include "anglecalibration.h"
#include "depthprofile.h"
#include "depthprofileview.h"
#include "efficiencydialog.h"
#include "elements.h"
#include "foildialog.h"
#include "project.h"
#include "splitview.h"
#include "version.h"

#include <cmath>
#include <memory>
#include <vector>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QMap>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QProgressDialog>
#include <QTextDocument>

namespace {
constexpr int PlotWidth = 900; // Pixels of the plot widgets, scaled to the width of the page
constexpr int PlotHeight = 520;
constexpr double FontSize = 9; // Points
constexpr double TableFontSize = 8;
constexpr int ElementsPerTable = 6; // Columns of elements, wider tables are split
constexpr int Resolution = 300; // Of the PDF, dpi

// A row of the concentration table, a depth profile
struct Row {
    Measurement *measurement = nullptr;
    DepthProfile *profile = nullptr;
    QString sample;
    QString measurementLabel; // With the profile name when the measurement has several rows
    bool renormalized = false;
    QMap<int, std::pair<double, double>> elements; // By Z: atomic fraction and its variance
    QString error;
};

// A plot on the pages after the text, taken from the tab that calculated it
struct Plot {
    QString title;
    std::unique_ptr<SpectrumPlotWidget> widget; // nullptr when it could not be calculated
};

QString escaped(const QString &text) {
    return text.toHtmlEscaped();
}

// The plot of a tab that is not shown, as a widget of its own: in the tab its layout would resize it when painted
std::unique_ptr<SpectrumPlotWidget> takePlot(SpectrumPlotWidget *plot) {
    plot->blockSignals(true); // Nothing is stored in the profile
    plot->setParent(nullptr);
    plot->resize(PlotWidth, PlotHeight);
    // Light colours whatever the theme of the program is
    QPalette palette = plot->palette();
    palette.setColor(QPalette::Window, Qt::white);
    palette.setColor(QPalette::WindowText, Qt::black);
    palette.setColor(QPalette::Mid, QColor(0xa0, 0xa0, 0xa0));
    plot->setPalette(palette);
    return std::unique_ptr<SpectrumPlotWidget>(plot);
}

QString number(double value, int precision = 4) {
    return QString::number(value, 'g', precision);
}

// The text of a settings item of a row, "—" when the measurement has no such settings
QString settingsText(const QString &item, const Measurement &measurement, const DepthProfile &profile) {
    const ExperimentalSettings *settings = measurement.settings;
    const DetectorSettings *detector = measurement.detector;
    const QString none = QStringLiteral("—");
    if(item == "Incident ion") {
        return settings && settings->beamZ > 0 ? Elements::displayLabel(settings->beamZ, settings->beamA) : none;
    }
    if(item == "Beam energy") {
        return settings ? number(settings->beamEnergy / Units::Megaelectronvolt) + " MeV" : none;
    }
    if(item == "Incident angle") {
        return settings ? number(settings->incidentAngle / Units::Degree) + "°" : none;
    }
    if(item == "Scattering angle") {
        return detector ? number(detector->scatteringAngle / Units::Degree) + "°" : none;
    }
    if(item == "ToF length") {
        return detector ? number(detector->tofLength / Units::Millimetre) + " mm" : none;
    }
    if(item == "Timing foil") {
        return detector ? foilText(detector->foilMaterial, detector->foilThickness) : none;
    }
    if(item == "ToF calibration") {
        return detector ? QString("%1 ns/ch, %2 ns").arg(number(detector->tofSlope / Units::Nanosecond, 6),
                                                         number(detector->tofOffset / Units::Nanosecond, 6))
                        : none;
    }
    if(item == "Angle calibration") {
        if(!detector || !detector->hasAngleCalibration()) {
            return QStringLiteral("none");
        }
        const AngleCalibration &calibration = detector->angleCalibration;
        QString text = QString("± %1° (window %2 mm at %3 mm), timing %4 %5 %6 ticks/ch × ToF ± %7 ticks")
                               .arg(number(windowHalfAngle(*detector) / Units::Degree),
                                    number(detector->windowWidth / Units::Millimetre),
                                    number(detector->detectorDistance / Units::Millimetre),
                                    number(calibration.centre, 6), calibration.centreSlope < 0 ? "−" : "+",
                                    number(std::abs(calibration.centreSlope), 3), number(calibration.halfWidth));
        if(calibration.reversed) {
            text += QStringLiteral(", larger timing is a smaller angle");
        }
        if(calibration.timingSmoothing > 0) {
            text += QString(", smoothing ± %1 ticks").arg(number(calibration.timingSmoothing));
        }
        text += profile.kinematicCorrection ? QStringLiteral(", kinematic correction") : QStringLiteral(", not used");
        return text;
    }
    if(item == "Efficiencies") {
        return detector ? efficiencyText(detector->efficiencies) : none;
    }
    if(item == "Physics") {
        const PhysicsSettings &physics = measurement.physicsOrDefault();
        QString text = QString("%1 screening, stopping %2").arg(screeningModelName(physics.screening),
                                                                physics.stoppingFile.isEmpty() ? "by JIBAL" : physics.stoppingFile);
        if(physics.electronicFactor != 1 || physics.nuclearFactor != 1) {
            text += QString(" × %1 (electronic), × %2 (nuclear)").arg(number(physics.electronicFactor),
                                                                    number(physics.nuclearFactor));
        }
        if(!physics.corrections.isEmpty()) {
            text += QString(", %1 element pair corrections").arg(physics.corrections.size());
        }
        if(physics.slowingDown != SlowingDownMethod::Midpoint) {
            text += physics.slowingDown == SlowingDownMethod::Euler ? QStringLiteral(", first-order slowing down")
                                                                    : QStringLiteral(", Runge-Kutta slowing down");
        }
        return text;
    }
    // Profile parameters, depths in 1e15 at./cm²
    const double thinFilm = Units::ThinFilmUnit;
    QString text = QString("bins %1, stopping bins %2, smoothing %3, 100 % over %4")
                           .arg(number(profile.binWidth / thinFilm), number(profile.stoppingBinWidth / thinFilm))
                           .arg(profile.minStoppingBinEvents)
                           .arg(profile.autoNormalization ? QStringLiteral("auto")
                                                          : QString("%1–%2").arg(number(profile.normalizationMin / thinFilm),
                                                                                 number(profile.normalizationMax / thinFilm)));
    if(!profile.averageFollowsNormalization) {
        text += QString(", averages over %1–%2").arg(number(profile.averageMin / thinFilm), number(profile.averageMax / thinFilm));
    }
    if(profile.rangeMin > 0 || profile.rangeMax < 1) {
        text += QString(", events of %1–%2 %").arg(number(profile.rangeMin * 100), number(profile.rangeMax * 100));
    }
    if(!profile.cutRanges.isEmpty()) {
        text += QString(", %1 selection(s) with parts of their own").arg(profile.cutRanges.size());
    }
    return text;
}
}

// "Sample", "Measurement" and the given columns, one block of rows per call
QString tableHeader(bool measurementColumn, const QStringList &columns) {
    QString html = "<table border='0.5' cellspacing='0' cellpadding='3'><tr><th style='white-space: nowrap'>Sample</th>";
    if(measurementColumn) {
        html += "<th style='white-space: nowrap'>Measurement</th>";
    }
    for(const QString &column : columns) {
        html += "<th style='white-space: nowrap'>" + escaped(column) + "</th>";
    }
    return html + "</tr>";
}

QString labelCells(const Row &row, bool measurementColumn, const QString &marker) {
    QString html = "<td style='white-space: nowrap'>" + escaped(row.sample) + (measurementColumn ? QString() : marker) + "</td>";
    if(measurementColumn) {
        html += "<td style='white-space: nowrap'>" + escaped(row.measurementLabel) + marker + "</td>";
    }
    return html;
}

bool writeReportPdf(Project &project, Report &report, const QString &fileName, QWidget *parent, QString &errorMessage) {
    errorMessage.clear();
    // Rows and split analyses in the order of the project
    std::vector<Row> rows;
    QList<std::pair<Measurement *, SplitAnalysis *>> splits;
    QMap<const Sample *, int> rowsOfSample;
    QMap<const Measurement *, int> rowsOfMeasurement;
    for(size_t s = 0; s < project.sampleCount(); s++) {
        Sample *sample = project.sample(s);
        for(const auto &measurement : sample->measurements) {
            for(const auto &profile : measurement->depthProfiles) {
                if(report.profiles.contains(profile.get())) {
                    rows.push_back({measurement.get(), profile.get(), sample->name});
                    rowsOfSample[sample]++;
                    rowsOfMeasurement[measurement.get()]++;
                }
            }
            for(const auto &split : measurement->splitAnalyses) {
                if(report.splits.contains(split.get())) {
                    splits.append({measurement.get(), split.get()});
                }
            }
        }
    }
    const bool severalRows = std::any_of(rowsOfSample.begin(), rowsOfSample.end(), [](int count) { return count > 1; });
    const bool measurementColumn = report.measurementNames == Report::NameMode::On
                                   || (report.measurementNames == Report::NameMode::Auto && severalRows);
    for(Row &row : rows) {
        row.measurementLabel = row.measurement->name;
        if(rowsOfMeasurement.value(row.measurement) > 1) {
            row.measurementLabel += QStringLiteral(" / ") + row.profile->name;
            if(!measurementColumn) {
                row.sample += QString(" (%1)").arg(row.profile->name);
            }
        }
    }

    QProgressDialog progress("Generating the report...", "Cancel", 0, static_cast<int>(rows.size() + splits.size()) + 1, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    // Depth profiles: calculated by a tab that is not shown, so that they are as in the program
    QSet<int> elements;
    std::vector<Plot> profilePlots;
    int step = 0;
    for(Row &row : rows) {
        progress.setLabelText(QString("Calculating %1 / %2...").arg(row.measurement->name, row.profile->name));
        progress.setValue(step++);
        if(progress.wasCanceled()) {
            return false;
        }
        DepthProfileView view(row.measurement, row.profile);
        view.calculateNow();
        row.error = view.errorMessage();
        if(!row.error.isEmpty()) {
            continue;
        }
        const DepthProfileResult &result = view.result();
        row.renormalized = !row.profile->averageFollowsNormalization;
        for(size_t c = 0; c < result.curves.size(); c++) {
            const DepthProfileResult::Average &average = result.averages[c];
            const double value = row.renormalized ? average.renormalized : average.concentration;
            const double uncertainty = row.renormalized ? average.renormalizedUncertainty : average.uncertainty;
            if(std::isnan(value)) {
                continue; // Left out of the renormalized averages
            }
            auto &[sum, variance] = row.elements[result.curves[c].z];
            sum += value;
            variance += uncertainty * uncertainty;
            elements.insert(result.curves[c].z);
        }
        std::unique_ptr<SpectrumPlotWidget> plot = takePlot(view.plot());
        if(report.rangeMode != Report::RangeMode::Gui) {
            const PlotView range = report.rangeMode == Report::RangeMode::Common
                                           ? report.commonView
                                           : report.profileViews.value(row.profile, row.profile->view.value_or(report.commonView));
            plot->setLogScale(range.logScale);
            plot->setFixedRange(range.depthMin / Units::ThinFilmUnit, range.depthMax / Units::ThinFilmUnit,
                                range.concentrationMin * 100, range.concentrationMax * 100);
        }
        const QString title = measurementColumn ? QString("%1 / %2").arg(row.sample, row.measurementLabel) : row.sample;
        profilePlots.push_back({title, std::move(plot)});
    }
    std::vector<Plot> splitPlots;
    for(const auto &[measurement, split] : splits) {
        progress.setLabelText(QString("Calculating %1 / %2...").arg(measurement->name, split->name));
        progress.setValue(step++);
        if(progress.wasCanceled()) {
            return false;
        }
        const Sample *sample = project.sampleOf(measurement);
        Plot plot{QString("%1 / %2 / %3").arg(sample ? sample->name : QString(), measurement->name, split->name), nullptr};
        SplitView view(measurement, split);
        if(view.plot() && view.errorMessage().isEmpty()) {
            plot.widget = takePlot(view.plot());
        }
        splitPlots.push_back(std::move(plot));
    }
    progress.setLabelText("Writing the PDF...");
    progress.setValue(step);

    QString html;
    html += QString("<h1>%1</h1>").arg(escaped(report.title));
    html += QString("<p>Project %1 (%2)<br>Generated %3 with JaRA %4</p>")
                    .arg(escaped(project.displayName()), escaped(QFileInfo(project.fileName()).fileName()),
                         QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"), escaped(Version::version()));
    const QString tableStyle = QString("<div style='font-size: %1pt'>").arg(TableFontSize);

    // Settings: values shared by all rows in a list, the others in a table
    QStringList items;
    const ReportSettings &chosen = report.settings;
    const std::pair<bool, const char *> available[] = {
            {chosen.ion, "Incident ion"}, {chosen.energy, "Beam energy"}, {chosen.incidentAngle, "Incident angle"},
            {chosen.scatteringAngle, "Scattering angle"}, {chosen.tofLength, "ToF length"}, {chosen.foil, "Timing foil"},
            {chosen.tofCalibration, "ToF calibration"}, {chosen.angleCalibration, "Angle calibration"},
            {chosen.efficiencies, "Efficiencies"}, {chosen.physics, "Physics"},
            {chosen.profileParameters, "Depth profile"}};
    for(const auto &[on, item] : available) {
        if(on) {
            items.append(QString::fromLatin1(item));
        }
    }
    if(!items.isEmpty() && !rows.empty()) {
        QStringList common;
        QStringList varying;
        for(const QString &item : items) {
            const QString first = settingsText(item, *rows.front().measurement, *rows.front().profile);
            const bool same = std::all_of(rows.begin(), rows.end(), [&](const Row &row) {
                return settingsText(item, *row.measurement, *row.profile) == first;
            });
            if(same) {
                common.append(QString("%1: %2").arg(escaped(item), escaped(first)));
            } else {
                varying.append(item);
            }
        }
        html += "<h2>Settings</h2>";
        if(!common.isEmpty()) {
            html += "<p>" + common.join("<br>") + "</p>";
        }
        if(!varying.isEmpty()) {
            html += tableStyle + tableHeader(measurementColumn, varying);
            for(const Row &row : rows) {
                html += "<tr>" + labelCells(row, measurementColumn, QString());
                for(const QString &item : varying) {
                    html += "<td>" + escaped(settingsText(item, *row.measurement, *row.profile)) + "</td>";
                }
                html += "</tr>";
            }
            html += "</table></div>";
        }
    }

    // Concentrations, elements by Z, in blocks of columns that fit on the page
    QList<int> columns = elements.values();
    std::sort(columns.begin(), columns.end());
    html += "<h2>Average concentrations (at.%)</h2>";
    const bool anyRenormalized = std::any_of(rows.begin(), rows.end(), [](const Row &row) { return row.renormalized; });
    for(qsizetype first = 0; first < columns.size() || first == 0; first += ElementsPerTable) {
        const QList<int> block = columns.mid(first, ElementsPerTable);
        QStringList names;
        for(const int z : block) {
            names.append(Elements::symbol(z));
        }
        html += tableStyle + tableHeader(measurementColumn, names);
        for(const Row &row : rows) {
            html += "<tr>" + labelCells(row, measurementColumn, row.renormalized ? QStringLiteral(" *") : QString());
            for(const int z : block) {
                const auto element = row.elements.find(z);
                html += "<td align='right' style='white-space: nowrap'>"
                        + (element == row.elements.end()
                                   ? QString()
                                   : escaped(percentWithUncertainty(element->first, std::sqrt(element->second), row.profile->roundTableConcentrations, false)))
                        + "</td>";
            }
            html += "</tr>";
        }
        html += "</table></div><p></p>";
        if(columns.isEmpty()) {
            break;
        }
    }
    QStringList notes{"Statistical uncertainties, including the normalization."};
    if(std::any_of(rows.begin(), rows.end(), [](const Row &row) { return row.profile && row.profile->roundTableConcentrations; })) {
        notes.append("< : 84 % upper limits of averages less than twice their uncertainty.");
    }
    if(anyRenormalized) {
        notes.append("* Averages over an interval other than the normalization interval, renormalized to 100 % among "
                     "the selections included in the renormalization.");
    }
    QStringList errors;
    for(const Row &row : rows) {
        if(!row.error.isEmpty()) {
            errors.append(QString("%1 %2: %3").arg(escaped(row.sample), escaped(row.measurementLabel), escaped(row.error)));
        }
    }
    html += "<p>" + notes.join("<br>") + "</p>";
    if(!errors.isEmpty()) {
        html += "<p>Not calculated:<br>" + errors.join("<br>") + "</p>";
    }

    // The text is laid out for the PDF and painted page by page, then the plots on pages of their own
    QPdfWriter writer(fileName);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
    writer.setResolution(Resolution);
    writer.setTitle(report.title);
    writer.setCreator(QString("JaRA %1").arg(Version::version()));
    QPainter painter;
    if(!painter.begin(&writer)) {
        errorMessage = QString("Could not write %1.").arg(fileName);
        return false;
    }
    const QRectF page = writer.pageLayout().paintRectPixels(Resolution);
    const QSizeF pageSize = page.size();
    QTextDocument document;
    document.documentLayout()->setPaintDevice(&writer);
    QFont font = QApplication::font();
    font.setPointSizeF(FontSize);
    document.setDefaultFont(font);
    document.setDocumentMargin(0);
    document.setPageSize(pageSize);
    document.setHtml(html);
    for(int i = 0; i < document.pageCount(); i++) {
        if(i > 0) {
            writer.newPage();
        }
        painter.save();
        painter.translate(0, -i * pageSize.height());
        document.drawContents(&painter, QRectF(0, i * pageSize.height(), pageSize.width(), pageSize.height()));
        painter.restore();
    }

    // Two plots per page, each with its title, scaled to the width of the page
    QFont titleFont = font;
    titleFont.setPointSizeF(FontSize * 1.3);
    titleFont.setBold(true);
    const double scale = pageSize.width() / PlotWidth;
    const double titleHeight = QFontMetricsF(titleFont, &writer).height() * 1.5;
    const double slotHeight = pageSize.height() / 2;
    auto drawPlots = [&](const QString &heading, std::vector<Plot> &plots) {
        for(size_t i = 0; i < plots.size(); i++) {
            if(i % 2 == 0) {
                writer.newPage();
            }
            const double top = static_cast<double>(i % 2) * slotHeight;
            painter.setFont(titleFont);
            painter.setPen(Qt::black);
            const QString title = i == 0 ? QString("%1: %2").arg(heading, plots[i].title) : plots[i].title;
            painter.drawText(QRectF(0, top, pageSize.width(), titleHeight), Qt::AlignLeft | Qt::AlignVCenter, title);
            if(!plots[i].widget) {
                painter.setFont(font);
                painter.drawText(QPointF(0, top + titleHeight * 1.5), QStringLiteral("Could not be calculated."));
                continue;
            }
            painter.save();
            painter.translate(0, top + titleHeight);
            painter.scale(scale, scale);
            plots[i].widget->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
            painter.restore();
        }
    };
    drawPlots(QStringLiteral("Depth profiles"), profilePlots);
    drawPlots(QStringLiteral("Appendix, split analyses"), splitPlots);
    painter.end();
    progress.setValue(progress.maximum());

    report.fingerprints.clear();
    for(const Row &row : rows) {
        report.fingerprints.insert(row.profile, depthProfileFingerprint(*row.measurement, *row.profile,
                                                                        report.rangeMode == Report::RangeMode::Gui));
    }
    report.optionsFingerprint = reportOptionsFingerprint(report, project.displayName(), [&project](const SplitAnalysis *split) {
        return project.measurementOf(split);
    });
    report.generated = QDateTime::currentDateTime();
    return true;
}
