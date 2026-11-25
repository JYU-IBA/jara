/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "elements.h"

#include <algorithm>
#include <cstdlib>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

extern "C" {
#include <jibal.h>
#include <jibal_config.h>
}

namespace {
jibal *jibalInstance = nullptr;

bool sameFile(const QString &a, const QString &b) {
    if(a.isEmpty() || b.isEmpty()) {
        return false;
    }
    const QString canonicalA = QFileInfo(a).canonicalFilePath();
    return !canonicalA.isEmpty() ? canonicalA == QFileInfo(b).canonicalFilePath() : QDir::cleanPath(a) == QDir::cleanPath(b);
}

const jibal_element *element(int z) {
    if(z < 1 || z > Elements::maxZ()) {
        return nullptr;
    }
    return &jibalInstance->elements[z]; // The elements table is indexed by Z
}
}

QString Elements::bundledConfigFile() {
    // Packages have their own JIBAL configuration and data files (release_scripts/deploy_mac.sh, and on other
    // platforms the install rules with JARA_BUNDLE_JIBAL_DATA in CMakeLists.txt)
#if defined(Q_OS_MACOS)
    const QString relative = QStringLiteral("/../Resources/jibal.conf");
#elif defined(Q_OS_WIN)
    const QString relative = QStringLiteral("/jibal.conf");
#else
    const QString relative = QStringLiteral("/../share/jara/jibal.conf");
#endif
    const QString file = QDir::cleanPath(QCoreApplication::applicationDirPath() + relative);
    return QFile::exists(file) ? file : QString();
}

QString Elements::userConfigFile() {
    char *file = jibal_config_user_config_filename();
    const QString result = file ? QString::fromLocal8Bit(file) : QString();
    free(file);
    return result;
}

QString Elements::configFileFor(JibalConfigSource source, const QString &file) {
    switch(source) {
        case JibalConfigSource::User:
            return userConfigFile();
        case JibalConfigSource::File:
            return file;
        case JibalConfigSource::Distribution:
            break;
    }
    return bundledConfigFile(); // Empty without one, JIBAL then finds its configuration itself
}

QString Elements::configFileInUse() {
    const char *file = jibalInstance ? jibal_config_filename(jibalInstance) : nullptr;
    return file ? QString::fromLocal8Bit(file) : QString();
}

QString Elements::sourceId(JibalConfigSource source) {
    switch(source) {
        case JibalConfigSource::User:
            return QStringLiteral("user");
        case JibalConfigSource::File:
            return QStringLiteral("file");
        case JibalConfigSource::Distribution:
            break;
    }
    return QStringLiteral("distribution");
}

JibalConfigSource Elements::sourceFromId(const QString &id) {
    if(id == QStringLiteral("user")) {
        return JibalConfigSource::User;
    }
    if(id == QStringLiteral("file")) {
        return JibalConfigSource::File;
    }
    return JibalConfigSource::Distribution;
}

QString Elements::jibalVersion() {
    return QString::fromUtf8(jibal_version());
}

QString Elements::configSourceInUse() {
    const QString file = configFileInUse();
    if(file.isEmpty()) {
        return QStringLiteral("none");
    }
    if(sameFile(file, bundledConfigFile())) {
        return sourceId(JibalConfigSource::Distribution);
    }
    if(sameFile(file, userConfigFile())) {
        return sourceId(JibalConfigSource::User);
    }
    return sourceId(JibalConfigSource::File);
}

QString Elements::configSourceDescription(const QString &id) {
    if(id == QStringLiteral("distribution")) {
        return QStringLiteral("installed with JaRA");
    }
    if(id == QStringLiteral("user")) {
        return QStringLiteral("JIBAL user configuration");
    }
    if(id == QStringLiteral("file")) {
        return QStringLiteral("other file");
    }
    return QStringLiteral("no configuration file");
}

bool Elements::init(JibalConfigSource source, const QString &file, QString &errorMessage) {
    cleanup();
    const QString configFile = configFileFor(source, file);
    // JIBAL would only say that the configuration could not be read
    if(!configFile.isEmpty() && !QFile::exists(configFile)) {
        errorMessage = QString("The JIBAL configuration file %1 does not exist.").arg(configFile);
        return false;
    }
    const QByteArray path = configFile.toLocal8Bit();
    jibalInstance = jibal_init(configFile.isEmpty() ? nullptr : path.constData());
    if(!jibalInstance) {
        errorMessage = QStringLiteral("JIBAL could not be initialized.");
        return false;
    }
    if(jibalInstance->error) {
        errorMessage = QString::fromUtf8(jibal_error_string(jibalInstance->error));
        cleanup();
        return false;
    }
    return true;
}

void Elements::cleanup() {
    if(jibalInstance) {
        jibal_free(jibalInstance);
        jibalInstance = nullptr;
    }
}

jibal *Elements::library() {
    return jibalInstance;
}

int Elements::maxZ() {
    return jibalInstance ? jibal_elements_Zmax(jibalInstance->elements) : 0;
}

bool Elements::isKnown(int z) {
    return element(z) != nullptr;
}

QString Elements::symbol(int z) {
    const jibal_element *e = element(z);
    return e ? QString::fromLatin1(e->name) : QString("Z%1").arg(z);
}

QString Elements::label(int z, int a) {
    return a > 0 ? QString("%1%2").arg(a).arg(symbol(z)) : symbol(z);
}

QString Elements::displayLabel(int z, int a) {
    if(a <= 0) {
        return symbol(z);
    }
    static const QString superscripts = QStringLiteral("\u2070\u00B9\u00B2\u00B3\u2074\u2075\u2076\u2077\u2078\u2079");
    QString mass;
    for(const QChar digit : QString::number(a)) {
        mass.append(superscripts.at(digit.digitValue()));
    }
    return mass + symbol(z);
}

QList<Elements::Isotope> Elements::isotopes(int z) {
    QList<Isotope> result;
    if(const jibal_element *e = element(z)) {
        for(size_t i = 0; i < e->n_isotopes; i++) {
            result.append({e->isotopes[i]->A, e->isotopes[i]->abundance, e->isotopes[i]->mass});
        }
    }
    return result;
}

bool Elements::hasIsotope(int z, int a) {
    const QList<Isotope> list = isotopes(z);
    return std::any_of(list.begin(), list.end(), [a](const Isotope &isotope) { return isotope.a == a; });
}

double Elements::abundance(int z, int a) {
    for(const Isotope &isotope : isotopes(z)) {
        if(isotope.a == a) {
            return isotope.abundance;
        }
    }
    return 0;
}

double Elements::mass(int z, int a) {
    double sum = 0;
    double abundances = 0;
    for(const Isotope &isotope : isotopes(z)) {
        if(a > 0 && isotope.a == a) {
            return isotope.mass;
        }
        sum += isotope.abundance * isotope.mass;
        abundances += isotope.abundance;
    }
    return a <= 0 && abundances > 0 ? sum / abundances : 0;
}

bool Elements::parse(const QString &text, int &z, int &a) {
    const QString trimmed = text.trimmed();
    bool isNumber = false;
    const int number = trimmed.toInt(&isNumber);
    if(isNumber) {
        if(!isKnown(number)) {
            return false;
        }
        z = number;
        a = 0;
        return true;
    }
    static const QRegularExpression pattern(QStringLiteral(R"(^(\d+)?\s*-?\s*([A-Za-z]{1,3})\s*-?\s*(\d+)?$)"));
    const QRegularExpressionMatch match = pattern.match(trimmed);
    if(!match.hasMatch() || (match.hasCaptured(1) && match.hasCaptured(3))) {
        return false;
    }
    const QString name = match.captured(2);
    int foundZ = 0;
    for(int i = 1; i <= maxZ(); i++) {
        if(QString::compare(symbol(i), name, Qt::CaseInsensitive) == 0) {
            foundZ = i;
            break;
        }
    }
    if(!foundZ) {
        return false;
    }
    const int mass = match.hasCaptured(1) ? match.captured(1).toInt() : match.captured(3).toInt();
    if(mass > 0 && !hasIsotope(foundZ, mass)) {
        return false;
    }
    z = foundZ;
    a = mass;
    return true;
}

QPoint Elements::tablePosition(int z) {
    if(z == 1) {
        return {0, 0};
    }
    if(z == 2) {
        return {17, 0};
    }
    if(z <= 10) {
        return z <= 4 ? QPoint(z - 3, 1) : QPoint(z - 5 + 12, 1);
    }
    if(z <= 18) {
        return z <= 12 ? QPoint(z - 11, 2) : QPoint(z - 13 + 12, 2);
    }
    if(z <= 36) {
        return {z - 19, 3};
    }
    if(z <= 54) {
        return {z - 37, 4};
    }
    if(z <= 56) {
        return {z - 55, 5};
    }
    if(z <= 71) {
        return {z - 57 + 3, LanthanideRow};
    }
    if(z <= 86) {
        return {z - 72 + 3, 5};
    }
    if(z <= 88) {
        return {z - 87, 6};
    }
    if(z <= 103) {
        return {z - 89 + 3, LanthanideRow + 1};
    }
    return {z - 104 + 3, 6};
}
