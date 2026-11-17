#include "elements.h"

#include <algorithm>

#include <QRegularExpression>

extern "C" {
#include <jibal.h>
}

namespace {
jibal *jibalInstance = nullptr;

const jibal_element *element(int z) {
    if(z < 1 || z > Elements::maxZ()) {
        return nullptr;
    }
    return &jibalInstance->elements[z]; // The elements table is indexed by Z
}
}

bool Elements::init(QString &errorMessage) {
    jibalInstance = jibal_init(nullptr);
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
            result.append({e->isotopes[i]->A, e->isotopes[i]->abundance});
        }
    }
    return result;
}

bool Elements::hasIsotope(int z, int a) {
    const QList<Isotope> list = isotopes(z);
    return std::any_of(list.begin(), list.end(), [a](const Isotope &isotope) { return isotope.a == a; });
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
