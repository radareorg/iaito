#ifndef CODEMETA_RANGE_H
#define CODEMETA_RANGE_H

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <optional>

struct CodeMetaRange
{
    size_t start = 0;
    size_t end = 0;
    size_t length = 0;
};

inline std::optional<CodeMetaRange> codeMetaRangeFromJson(const QJsonObject &obj, size_t codeLength)
{
    int start = obj["start"].toInt(-1);
    int end = obj["end"].toInt(-1);
    if (start < 0 || end < start) {
        return std::nullopt;
    }

    size_t rangeStart = static_cast<size_t>(start);
    size_t rangeEnd = std::min(static_cast<size_t>(end), codeLength);
    if (rangeStart >= codeLength || rangeEnd <= rangeStart) {
        return std::nullopt;
    }

    return CodeMetaRange{rangeStart, rangeEnd, rangeEnd - rangeStart};
}

inline std::optional<CodeMetaRange> codeMetaOffsetRangeFromJson(
    const QJsonObject &obj, const QString &code)
{
    int start = obj["start"].toInt(-1);
    int end = obj["end"].toInt(start);
    if (start < 0 || end < start || start >= code.size()) {
        return std::nullopt;
    }

    if (end == start) {
        int lineStart = code.lastIndexOf(QLatin1Char('\n'), start - 1);
        lineStart = (lineStart < 0) ? 0 : lineStart + 1;
        int lineEnd = code.indexOf(QLatin1Char('\n'), start);
        if (lineEnd < 0) {
            lineEnd = code.size();
        }
        if (lineEnd <= lineStart) {
            return std::nullopt;
        }
        return CodeMetaRange{
            static_cast<size_t>(lineStart),
            static_cast<size_t>(lineEnd),
            static_cast<size_t>(lineEnd - lineStart)};
    }

    size_t rangeStart = static_cast<size_t>(start);
    size_t rangeEnd = std::min(static_cast<size_t>(end), static_cast<size_t>(code.size()));
    if (rangeEnd <= rangeStart) {
        return std::nullopt;
    }
    return CodeMetaRange{rangeStart, rangeEnd, rangeEnd - rangeStart};
}

inline std::optional<qulonglong> codeMetaAddressFromJson(const QJsonValue &value)
{
    bool ok = false;
    qulonglong address = value.toVariant().toULongLong(&ok);
    if (ok) {
        return address;
    }

    QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    address = text.toULongLong(&ok, 0);
    if (!ok && text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        address = text.mid(2).toULongLong(&ok, 16);
    }
    if (!ok) {
        return std::nullopt;
    }
    return address;
}

inline std::optional<CodeMetaRange> intersectCodeMetaRange(
    size_t rangeStart, size_t rangeEnd, size_t scopeStart, size_t scopeEnd)
{
    if (rangeEnd <= rangeStart || scopeEnd <= scopeStart || rangeEnd <= scopeStart
        || rangeStart >= scopeEnd) {
        return std::nullopt;
    }

    size_t start = std::max(rangeStart, scopeStart) - scopeStart;
    size_t end = std::min(rangeEnd, scopeEnd) - scopeStart;
    return CodeMetaRange{start, end, end - start};
}

#endif // CODEMETA_RANGE_H
