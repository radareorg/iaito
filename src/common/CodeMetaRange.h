#ifndef CODEMETA_RANGE_H
#define CODEMETA_RANGE_H

#include <QJsonObject>

#include <algorithm>
#include <cstddef>
#include <optional>

struct CodeMetaRange
{
    size_t start;
    size_t end;
    size_t length;
};

inline std::optional<CodeMetaRange> codeMetaRangeFromJson(const QJsonObject &obj,
                                                          size_t codeLength)
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

    return CodeMetaRange { rangeStart, rangeEnd, rangeEnd - rangeStart };
}

inline std::optional<CodeMetaRange> intersectCodeMetaRange(size_t rangeStart, size_t rangeEnd,
                                                           size_t scopeStart, size_t scopeEnd)
{
    if (rangeEnd <= rangeStart || scopeEnd <= scopeStart || rangeEnd <= scopeStart
        || rangeStart >= scopeEnd) {
        return std::nullopt;
    }

    size_t start = std::max(rangeStart, scopeStart) - scopeStart;
    size_t end = std::min(rangeEnd, scopeEnd) - scopeStart;
    return CodeMetaRange { start, end, end - start };
}

#endif // CODEMETA_RANGE_H
