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
