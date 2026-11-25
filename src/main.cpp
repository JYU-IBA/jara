/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include <cmath>
#include <cstdio>
#include <cstring>

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QMessageBox>

extern "C" {
#include <jibal.h>
}

#include "defaults.h"
#include "elements.h"
#include "mainwindow.h"
#include "stopping.h"
#include "units.h"
#include "version.h"

namespace {
void setApplicationNames() {
    QCoreApplication::setApplicationName("JaRA");
    QCoreApplication::setOrganizationName("JYU");
    QCoreApplication::setOrganizationDomain("jyu.fi"); // Settings are stored as fi.jyu.JaRA on macOS
}

const char *sourceName(JibalConfigSource source) {
    switch(source) {
        case JibalConfigSource::User:
            return "JIBAL user configuration";
        case JibalConfigSource::File:
            return "file";
        case JibalConfigSource::Distribution:
            break;
    }
    return "distribution";
}

// Checks that JIBAL and its data files are found, e.g. in a package made for a release. Prints what was used and
// returns the exit status: 0 when the stopping of He in Si could be calculated, 1 otherwise. Uses the JIBAL
// configuration of the preferences, without falling back to the distribution one.
int checkInstallation() {
    std::printf("JaRA %s\n", qPrintable(Version::version()));
    const JibalConfigPreference preference = Defaults::jibalConfigPreference();
    std::printf("Configuration preference: %s\n", sourceName(preference.source));
    QString errorMessage;
    if(!Elements::init(preference.source, preference.file, errorMessage)) {
        std::printf("JIBAL could not be initialized: %s\n", qPrintable(errorMessage));
        return 1;
    }
    std::printf("Configuration in use: %s\n", qPrintable(Elements::configSourceDescription(Elements::configSourceInUse())));
    const jibal *library = Elements::library();
    const char *configFile = jibal_config_filename(library);
    std::printf("JIBAL %s\nConfiguration file: %s\nData directory: %s\nMasses file: %s\nStopping files: %zu\n",
                jibal_version(), configFile ? configFile : "(none)",
                library->config->datadir ? library->config->datadir : "(none)",
                library->config->masses_file ? library->config->masses_file : "(none)", library->gsto->n_files);
    const int z = 2;
    const double energy = 2 * Units::Megaelectronvolt;
    const StoppingTable table(z, Elements::mass(z, 4), {14}, PhysicsSettings());
    int result = 1;
    if(!table.isValid()) {
        std::printf("Stopping of He in Si could not be calculated: %s\n", qPrintable(table.errorMessage()));
    } else {
        const double fractions[] = {1.0};
        const double stopping = table.stopping(fractions, energy);
        std::printf("Stopping of 2 MeV 4He in Si: %g eV/(1e15 atoms/cm2)\n",
                    stopping / (C_EV / Units::ThinFilmUnit));
        std::printf("Stopping data of 4He in Si: %g–%g keV\n", table.minEnergy() / Units::Kiloelectronvolt,
                    table.maxEnergy() / Units::Kiloelectronvolt);
        if(std::isfinite(stopping) && stopping > 0) {
            result = 0;
        }
    }
    Elements::cleanup();
    std::printf("%s\n", result == 0 ? "OK" : "FAILED");
    return result;
}
}

int main(int argc, char *argv[]) {
    if(argc == 2 && std::strcmp(argv[1], "--check-installation") == 0) { // Without a GUI, also works without a display
        QCoreApplication app(argc, argv);
        setApplicationNames(); // For the preferences
        return checkInstallation();
    }
    QApplication app(argc, argv);
    setApplicationNames();
    QApplication::setApplicationVersion(Version::version());
#ifndef Q_OS_MACOS // The bundle icon is used on macOS, the window icon would replace it in the Dock
    QIcon icon;
    for(int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(QString(":/icons/jara_%1.png").arg(size));
    }
    QApplication::setWindowIcon(icon);
#endif
    // The JIBAL configuration of the preferences. When the user's own one fails, the distribution one is used, so
    // that JaRA works and the preference can be changed.
    const JibalConfigPreference preference = Defaults::jibalConfigPreference();
    QString errorMessage;
    bool initialized = Elements::init(preference.source, preference.file, errorMessage);
    if(!initialized && preference.source != JibalConfigSource::Distribution) {
        QMessageBox::warning(nullptr, "JaRA",
                             QString("JIBAL could not be initialized with the %1 %2:\n%3\n\nThe distribution "
                                     "configuration is used instead. Change it in Preferences → JIBAL.")
                                     .arg(sourceName(preference.source),
                                          Elements::configFileFor(preference.source, preference.file), errorMessage));
        initialized = Elements::init(JibalConfigSource::Distribution, {}, errorMessage);
    }
    if(!initialized) {
        QMessageBox::warning(nullptr, "JaRA", QString("Element data could not be loaded from JIBAL:\n%1").arg(errorMessage));
    }
    MainWindow w;
    if(argc > 1) {
        w.openProjectFile(QString::fromLocal8Bit(argv[1]));
    }
    w.show();
    const int result = QApplication::exec();
    Elements::cleanup();
    return result;
}
