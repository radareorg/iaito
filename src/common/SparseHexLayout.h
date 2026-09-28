#ifndef SPARSEHEXLAYOUT_H
#define SPARSEHEXLAYOUT_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace SparseHexLayout {

// Offsets are relative to a bounded read buffer, never absolute addresses.
struct Row
{
    int offset;
    int size;
    int fill = -1;
    bool continues = false;
};

inline int uniformPrefix(const unsigned char *bytes, int size)
{
    const uint64_t pattern = bytes[0] ? UINT64_MAX : 0;
    int i = 0;
    while (size - i >= int(sizeof(pattern))) {
        uint64_t word;
        std::memcpy(&word, bytes + i, sizeof(word));
        if (word != pattern) {
            break;
        }
        i += sizeof(word);
    }
    while (i < size && bytes[i] == bytes[0]) {
        ++i;
    }
    return i;
}

inline std::vector<Row> build(
    const unsigned char *bytes, int size, int rowSize, int itemSize, int maxRows)
{
    std::vector<Row> rows;
    if (size <= 0 || rowSize <= 0 || itemSize <= 0 || maxRows <= 0) {
        return rows;
    }
    rows.reserve(std::min(maxRows, size / rowSize + 2));
    int begin = 0;
    int pos = 0;
    while (pos < size && int(rows.size()) < maxRows) {
        int step = itemSize;
        if (bytes[pos] == 0 || bytes[pos] == 0xff) {
            const int run = uniformPrefix(bytes + pos, size - pos);
            if (run > rowSize * 2) {
                if (pos > begin) {
                    rows.push_back({begin, pos - begin});
                    if (int(rows.size()) == maxRows) {
                        return rows;
                    }
                }
                // Keep multi-byte items intact, including at the end of a read.
                const int folded = run - run % itemSize;
                rows.push_back({pos, folded, bytes[pos], pos + run == size});
                begin = pos = pos + folded;
                continue;
            }
            // Consume short runs once too; rescanning every suffix is quadratic
            // when the user selects a large number of columns.
            step = std::max(itemSize, run - run % itemSize);
        }
        step = std::min(step, size - pos);
        while (step > 0 && int(rows.size()) < maxRows) {
            const int count = std::min(step, rowSize - (pos - begin));
            pos += count;
            step -= count;
            if (pos - begin == rowSize) {
                rows.push_back({begin, rowSize});
                begin = pos;
            }
        }
    }
    if (pos > begin && int(rows.size()) < maxRows) {
        rows.push_back({begin, pos - begin});
    }
    return rows;
}

} // namespace SparseHexLayout

#endif // SPARSEHEXLAYOUT_H
