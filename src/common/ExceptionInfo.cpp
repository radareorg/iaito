#include "ExceptionInfo.h"

#include <cmath>
#include <QCoreApplication>
#include <QJsonObject>
#include <QVariant>

static QString exceptionTr(const char *text)
{
    return QCoreApplication::translate("ExceptionInfo", text);
}

static quint64 exceptionAddress(const QJsonValue &value)
{
    bool ok = false;
    quint64 result = ExceptionRegion::Invalid;
    if (value.isString()) {
        result = value.toString().toULongLong(&ok, 0);
    } else if (
        value.isDouble() && value.toDouble() >= 0
        && std::floor(value.toDouble()) == value.toDouble()) {
        result = value.toVariant().toULongLong(&ok);
    }
    return ok ? result : ExceptionRegion::Invalid;
}

bool ExceptionRegion::protects(quint64 address, quint64 size) const
{
    // Subtraction avoids overflowing a block's end near the address-space limit.
    return size && address < to && (address >= from || from - address < size);
}

bool ExceptionRegion::touches(quint64 address, quint64 size) const
{
    return protects(address, size)
           || (handler != Invalid && handler >= address && handler - address < size);
}

QString ExceptionRegion::label() const
{
    if (kind == QStringLiteral("cleanup")) {
        return exceptionTr("Cleanup / finally");
    }
    const QString name = kind == QStringLiteral("filter") ? exceptionTr("Filter")
                                                          : exceptionTr("Catch");
    return QStringLiteral("%1 (%2)").arg(
        name,
        catchAll         ? exceptionTr("all exceptions")
        : type.isEmpty() ? exceptionTr("unknown type")
                         : type);
}

QString ExceptionRegion::description() const
{
    QString text = exceptionTr("#%1 · %2\nProtected: [0x%3, 0x%4)\nHandler: 0x%5")
                       .arg(index)
                       .arg(label())
                       .arg(from, 0, 16)
                       .arg(to, 0, 16)
                       .arg(handler, 0, 16);
    if (source != Invalid) {
        text += exceptionTr("\nMetadata source: 0x%1").arg(source, 0, 16);
    }
    if (filter && filter != Invalid) {
        text += exceptionTr("\nFilter value / address: 0x%1").arg(filter, 0, 16);
    }
    if (typeFilter) {
        text += exceptionTr("\nType filter: %1").arg(typeFilter);
    }
    return text;
}

QColor ExceptionRegion::color() const
{
    // Stable across views and independent of metadata order; shared ranges share a color.
    const quint64 hash = (from >> 2) ^ (to >> 1) ^ from;
    return QColor::fromHsv(int(hash % 12) * 30, 145, 210);
}

QList<ExceptionRegion> ExceptionRegion::parse(const QJsonArray &array)
{
    QList<ExceptionRegion> result;
    for (int i = 0; i < array.size(); ++i) {
        const auto object = array.at(i).toObject();
        ExceptionRegion region;
        region.from = exceptionAddress(object.value(QStringLiteral("from")));
        region.to = exceptionAddress(object.value(QStringLiteral("to")));
        region.handler = exceptionAddress(object.value(QStringLiteral("handler")));
        if (region.from == Invalid || region.to == Invalid || region.from >= region.to
            || region.handler == Invalid) {
            continue;
        }
        region.index = exceptionAddress(object.value(QStringLiteral("index")));
        if (region.index == Invalid) {
            region.index = quint64(i);
        }
        region.source = exceptionAddress(object.value(QStringLiteral("source")));
        region.filter = exceptionAddress(object.value(QStringLiteral("filter")));
        region.kind = object.value(QStringLiteral("kind")).toString();
        region.type = object.value(QStringLiteral("type")).toString();
        region.typeFilter = object.value(QStringLiteral("typeFilter")).toVariant().toLongLong();
        region.catchAll = object.value(QStringLiteral("catchAll")).toBool();
        result.append(region);
    }
    return result;
}

QList<ExceptionRegion> ExceptionRegion::forBlocks(
    const QList<ExceptionRegion> &regions, const QJsonArray &blocks)
{
    QList<ExceptionRegion> result;
    for (const auto &region : regions) {
        for (const auto &value : blocks) {
            const auto block = value.toObject();
            const auto address = exceptionAddress(block.value(QStringLiteral("addr")));
            const auto size = exceptionAddress(block.value(QStringLiteral("size")));
            if (address != Invalid && size != Invalid && region.touches(address, size)) {
                result.append(region);
                break;
            }
        }
    }
    return result;
}
