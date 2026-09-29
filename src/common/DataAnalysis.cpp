#include "DataAnalysis.h"

#include <algorithm>
#include <cmath>

namespace DataAnalysis {

Range sizedRange(RVA start, RVA size)
{
    return {start, start + qMin(size, RVA_MAX - start)};
}

Range intersect(Range a, Range b)
{
    const RVA start = qMax(a.start, b.start);
    return {start, qMax(start, qMin(a.end, b.end))};
}

Range bucket(Range range, int index, int count)
{
    if (!range.valid() || count <= 0 || index < 0 || index >= count) {
        return {};
    }
    // Avoid both floating point address rounding and multiplication overflow.
    auto boundary = [range, count](RVA i) {
        return range.start + (range.size() / count) * i + (range.size() % count) * i / count;
    };
    return {boundary(index), boundary(index + 1)};
}

Range centered(Range domain, RVA address, RVA size)
{
    if (!domain.valid()) {
        return {};
    }
    size = qBound<RVA>(1, size, domain.size());
    address = qBound(domain.start, address, domain.end - 1);
    const RVA start = address - qMin(address - domain.start, size / 2);
    const RVA clamped = qMin(start, domain.end - size);
    return {clamped, clamped + size};
}

QVector<Range> mergeRanges(QVector<Range> ranges)
{
    std::sort(ranges.begin(), ranges.end(), [](Range a, Range b) { return a.start < b.start; });
    QVector<Range> result;
    for (auto range : ranges) {
        if (!range.valid()) {
            continue;
        }
        if (result.isEmpty() || result.last().end < range.start) {
            result.append(range);
        } else {
            result.last().end = qMax(result.last().end, range.end);
        }
    }
    return result;
}

Sample sample(
    Range range,
    const QVector<Range> &readable,
    const std::function<QByteArray(RVA, int)> &read,
    int budget)
{
    Sample result;
    result.range = range;
    if (!range.valid() || budget <= 0) {
        return result;
    }
    QVector<Range> spans;
    for (auto candidate : readable) {
        const Range span = intersect(range, candidate);
        if (span.valid()) {
            spans.append(span);
            result.covered += span.size();
        }
    }
    std::array<int, 256> histogram{};
    RVA remaining = result.covered;
    for (auto span : spans) {
        if (budget <= 0) {
            break;
        }
        // Allocate the budget proportionally among disjoint readable spans.
        const int share = qMax(1, int((long double) span.size() / remaining * budget));
        const int length = int(qMin<RVA>(span.size(), qMin(budget, share)));
        const RVA address = span.start + (span.size() - length) / 2;
        const QByteArray data = read(address, length).left(length);
        for (unsigned char byte : data) {
            ++histogram[byte];
        }
        result.bytes += data.size();
        budget -= length;
        remaining -= span.size();
    }
    if (!result.bytes) {
        return result;
    }
    auto &v = result.values;
    for (int byte = 0; byte < 256; ++byte) {
        const int n = histogram[byte];
        if (!n) {
            continue;
        }
        const double p = double(n) / result.bytes;
        v[Entropy] -= p * std::log2(p) / 8.0;
        v[Printable] += (byte >= 32 && byte <= 126) ? p : 0;
        v[Zero] += byte == 0 ? p : 0;
        v[FF] += byte == 255 ? p : 0;
        v[Unique] += 1.0 / 256;
        v[Mean] += p * byte / 255;
        v[Alpha] += ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')) ? p : 0;
        v[Digit] += (byte >= '0' && byte <= '9') ? p : 0;
        v[Space] += (byte == ' ' || (byte >= 9 && byte <= 13)) ? p : 0;
        v[Control] += (byte < 32 || byte == 127) ? p : 0;
        v[High] += byte >= 128 ? p : 0;
    }
    return result;
}

} // namespace DataAnalysis
