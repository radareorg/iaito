#ifndef DATAANALYSIS_H
#define DATAANALYSIS_H

#include "core/IaitoCommon.h"

#include <array>
#include <functional>
#include <QByteArray>
#include <QVector>

namespace DataAnalysis {

// Half-open ranges, including when the exclusive end is RVA_MAX.
struct Range
{
    RVA start = 0;
    RVA end = 0;
    bool valid() const { return start < end; }
    RVA size() const { return end - start; }
    bool contains(RVA address) const { return start <= address && address < end; }
    bool operator==(const Range &other) const { return start == other.start && end == other.end; }
    bool operator!=(const Range &other) const { return !(*this == other); }
};

enum Metric {
    Entropy,
    Printable,
    Zero,
    FF,
    Unique,
    Mean,
    Alpha,
    Digit,
    Space,
    Control,
    High,
    Count
};

struct Sample
{
    Range range;
    RVA covered = 0;
    int bytes = 0;
    std::array<double, Count> values{}; // Normalized to [0, 1]; entropy / 8.
};

Range sizedRange(RVA start, RVA size);
Range intersect(Range a, Range b);
Range bucket(Range range, int index, int count);
Range centered(Range domain, RVA address, RVA size);
QVector<Range> mergeRanges(QVector<Range> ranges);

// Bounded, deterministic sampling. Reads only known readable spans; missing bytes
// never contribute fabricated 0xff data. Large buckets use centered windows.
Sample sample(
    Range range,
    const QVector<Range> &readable,
    const std::function<QByteArray(RVA, int)> &read,
    int budget = 4096);

} // namespace DataAnalysis

#endif
