#ifndef EXCEPTIONINFO_H
#define EXCEPTIONINFO_H

#include <limits>
#include <QColor>
#include <QJsonArray>
#include <QList>
#include <QString>

// Addresses in iwj use half-open ranges: [from, to).
struct ExceptionRegion
{
    static constexpr quint64 Invalid = std::numeric_limits<quint64>::max();
    quint64 index = 0;
    quint64 source = Invalid;
    quint64 from = Invalid;
    quint64 to = Invalid;
    quint64 handler = Invalid;
    quint64 filter = 0;
    QString kind;
    QString type;
    qint64 typeFilter = 0;
    bool catchAll = false;

    bool contains(quint64 address) const { return from <= address && address < to; }
    bool protects(quint64 address, quint64 size) const;
    bool touches(quint64 address, quint64 size) const;
    QString label() const;
    QString description() const;
    QColor color() const;
    static QList<ExceptionRegion> parse(const QJsonArray &array);
    static QList<ExceptionRegion> forBlocks(
        const QList<ExceptionRegion> &regions, const QJsonArray &blocks);
};

#endif
