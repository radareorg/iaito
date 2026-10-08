#pragma once

#include <QList>
#include <QString>

struct SourceLineReference
{
    quint64 address;
    int line;
    QString code;

    // fs.r2's /cl files contain address, line number and source text, separated by tabs.
    static QList<SourceLineReference> parse(const QString &contents);
};
