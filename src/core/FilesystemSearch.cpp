#include "core/FilesystemEvidence.h"
#include "core/Iaito.h"

#include <algorithm>
#include <array>
#include <optional>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QtEndian>

namespace {
using Evidence = FilesystemEvidence;
constexpr int MaxHits = 10000;

struct Signature
{
    QByteArray bytes;
    QString description;
};

quint32 pngCrc(quint32 crc, const QByteArray &data)
{
    static constexpr auto table = []() {
        std::array<quint32, 256> entries{};
        for (quint32 i = 0; i < entries.size(); ++i) {
            quint32 value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value >> 1) ^ ((value & 1) ? 0xedb88320U : 0U);
            }
            entries[i] = value;
        }
        return entries;
    }();
    for (unsigned char byte : data) {
        crc = (crc >> 8) ^ table[(crc ^ byte) & 0xffU];
    }
    return crc;
}

QString message(const char *text)
{
    return QCoreApplication::translate("FilesystemSearch", text);
}

QList<Signature> signatures(Evidence::SearchKind kind, const QString &term)
{
    if (kind == Evidence::SearchKind::Signature) {
        return {
            {QByteArray::fromHex("89504e470d0a1a0a"), QStringLiteral("PNG")},
            {QByteArray::fromHex("ffd8ff"), QStringLiteral("JPEG")},
            {QByteArray("%PDF-"), QStringLiteral("PDF")},
            {QByteArray::fromHex("504b0304"), QStringLiteral("ZIP")},
            {QByteArray("GIF87a"), QStringLiteral("GIF")},
            {QByteArray("GIF89a"), QStringLiteral("GIF")},
            {QByteArray::fromHex("7f454c46"), QStringLiteral("ELF")},
        };
    }
    if (kind == Evidence::SearchKind::Hex) {
        static const QRegularExpression separators(QStringLiteral("\\s+"));
        static const QRegularExpression hex(QStringLiteral("^[a-fA-F0-9]+$"));
        QString compact = term;
        compact.remove(separators);
        if (compact.size() % 2 || !hex.match(compact).hasMatch()) {
            return {};
        }
        const auto bytes = QByteArray::fromHex(compact.toLatin1());
        return {{bytes, QString::fromLatin1(bytes.toHex(' '))}};
    }
    return {{term.toUtf8(), term}};
}
} // namespace

FilesystemEvidence::SearchResult FilesystemEvidence::findNames(
    int index, const QString &pattern, const Progress &progress) const
{
    const std::lock_guard lock(ioMutex);
    SearchResult result;
    const auto &volume = imageVolumes.at(index);
    if (!selectVolume(volume)) {
        return result;
    }
    if (pattern.trimmed().isEmpty()) {
        result.status = WalkStatus::Error;
        result.error = message(QT_TRANSLATE_NOOP("FilesystemSearch", "Enter a name or glob."));
        return result;
    }
    const auto glob = pattern.toUtf8();
    std::optional<RCoreLocked> legacy;
    if (!volume.deletedEntries) {
        legacy.emplace(Core());
    }
    for (bool deleted : {false, true}) {
        if (progress && !progress(0, 0)) {
            result.status = WalkStatus::Stopped;
            return result;
        }
#if R2_ABIVERSION >= 187
        if (deleted && !volume.deletedEntries) {
            continue;
        }
        auto *list = r_fs_find_name(
            fs,
            volume.mount.constData(),
            glob.constData(),
            deleted ? R_FS_VIEW_DELETED : R_FS_VIEW_NORMAL);
#else
        if (deleted) {
            continue;
        }
        auto *list = r_fs_find_name(fs, volume.mount.constData(), glob.constData());
#endif
        const std::unique_ptr<RList, decltype(&r_list_free)> paths(list, r_list_free);
        RListIter *iter;
        const char *path;
        IaitoRListForeach(list, iter, const char, path)
        {
            const QByteArray fullPath(path);
            if (fullPath.startsWith(volume.mount + '/')) {
                const auto local = fullPath.mid(volume.mount.size());
                result.hits.append({QString::fromUtf8(local), UT64_MAX, 0, 0, 0, deleted, local});
            }
            if (result.hits.size() == MaxHits) {
                result.limited = true;
                result.status = WalkStatus::Stopped;
                return result;
            }
        }
    }
    result.status = WalkStatus::Complete;
    return result;
}

FilesystemEvidence::SearchResult FilesystemEvidence::search(
    int index, SearchKind kind, quint32 scope, const QString &term, const Progress &progress) const
{
    if (kind == SearchKind::Name) {
        return findNames(index, term, progress);
    }
    SearchResult result;
    const auto patterns = signatures(kind, term);
    if (patterns.isEmpty() || patterns.first().bytes.isEmpty()
        || patterns.first().bytes.size() > 4096) {
        result.status = WalkStatus::Error;
        result.error
            = kind == SearchKind::Hex
                  ? message(QT_TRANSLATE_NOOP("FilesystemSearch", "Enter hexadecimal byte pairs."))
                  : message(QT_TRANSLATE_NOOP("FilesystemSearch", "Enter text up to 4096 bytes."));
        return result;
    }
    const auto &volume = imageVolumes.at(index);
    QList<Range> ranges;
    if (scope) {
        const auto mapping = allocationMapping(index, scope, [&progress](quint64, quint64) {
            return !progress || progress(0, 0);
        });
        if (mapping.status != WalkStatus::Complete) {
            result.status = mapping.status;
            return result;
        }
        ranges = mapping.ranges;
    } else {
        ranges.append({0, volume.start, volume.length, 0});
    }
    quint64 total = 0;
    for (const auto &range : ranges) {
        total += range.length;
    }
    quint64 scanned = 0;
    struct Scan
    {
        const QList<Signature> &patterns;
        SearchResult &result;
        const Range *range = nullptr;
    } scan{patterns, result};
    const auto hit = [](RSearchKeyword *keyword, void *data, ut64 offset) {
        auto &scan = *static_cast<Scan *>(data);
        const auto &range = *scan.range;
        const quint64 end = range.physical + range.length;
        if (offset >= range.physical && offset < end && keyword->keyword_length <= end - offset) {
            scan.result.hits.append(
                {scan.patterns.at(keyword->kwidx).description,
                 offset,
                 keyword->keyword_length,
                 end - offset,
                 range.flags,
                 false,
                 {}});
        }
        scan.result.limited = scan.result.hits.size() >= MaxHits;
        return scan.result.limited ? 2 : 1;
    };
    for (const auto &range : ranges) {
        if (progress && !progress(scanned, total)) {
            result.status = WalkStatus::Stopped;
            return result;
        }
        std::unique_ptr<RSearch, decltype(&r_search_free)>
            search(r_search_new(R_SEARCH_KEYWORD), r_search_free);
        if (!search) {
            result.status = WalkStatus::Error;
            return result;
        }
        search->contiguous = true;
        search->overlap = true;
        for (const auto &pattern : patterns) {
            auto *keyword = r_search_keyword_new(
                reinterpret_cast<const ut8 *>(pattern.bytes.data()),
                int(pattern.bytes.size()),
                nullptr,
                0,
                nullptr);
            if (!keyword || !r_search_kw_add(search.get(), keyword)) {
                r_search_keyword_free(keyword);
                result.status = WalkStatus::Error;
                return result;
            }
        }
        scan.range = &range;
        r_search_set_callback(search.get(), hit, &scan);
        for (quint64 offset = 0; offset < range.length;) {
            const int count = int(qMin<quint64>(256 * 1024, range.length - offset));
            const auto page = readImage(range.physical + offset, count);
            if (page.error || page.bytes.size() != count) {
                result.status = WalkStatus::Error;
                return result;
            }
            const int found = r_search_update(
                search.get(),
                range.physical + offset,
                reinterpret_cast<const ut8 *>(page.bytes.data()),
                count);
            if (result.limited) {
                result.status = WalkStatus::Stopped;
                return result;
            }
            if (found < 0) {
                result.status = WalkStatus::Error;
                return result;
            }
            offset += count;
            scanned += count;
            if (progress && !progress(scanned, total)) {
                result.status = WalkStatus::Stopped;
                return result;
            }
        }
    }
    result.status = WalkStatus::Complete;
    std::stable_sort(
        result.hits.begin(), result.hits.end(), [](const SearchHit &a, const SearchHit &b) {
            return a.offset < b.offset;
        });
    return result;
}

quint64 FilesystemEvidence::validatedPngLength(const SearchHit &hit, const Progress &progress) const
{
    const auto signature = QByteArray::fromHex("89504e470d0a1a0a");
    if (hit.offset >= imageSize || hit.rangeRemaining > imageSize - hit.offset
        || hit.rangeRemaining < 8 || readImage(hit.offset, 8).bytes != signature) {
        return 0;
    }
    quint64 offset = 8;
    bool hasData = false;
    while (hit.rangeRemaining - offset >= 12) {
        const auto header = readImage(hit.offset + offset, 8).bytes;
        if (header.size() != 8 || (progress && !progress(offset, hit.rangeRemaining))) {
            return 0;
        }
        const auto type = header.mid(4);
        const auto length = qFromBigEndian<quint32>(header.constData());
        if (length > 0x7fffffff || length > hit.rangeRemaining - offset - 12
            || (offset == 8 && (type != "IHDR" || length != 13))
            || (offset != 8 && type == "IHDR")) {
            return 0;
        }
        if (type == "IHDR") {
            const auto dimensions = readImage(hit.offset + offset + 8, 8).bytes;
            if (dimensions.size() != 8 || !qFromBigEndian<quint32>(dimensions.constData())
                || !qFromBigEndian<quint32>(dimensions.constData() + 4)) {
                return 0;
            }
        }
        quint32 crc = pngCrc(0xffffffffU, type);
        for (quint64 read = 0; read < length;) {
            const int count = int(qMin<quint64>(64 * 1024, length - read));
            const auto bytes = readImage(hit.offset + offset + 8 + read, count).bytes;
            if (bytes.size() != count
                || (progress && !progress(offset + read, hit.rangeRemaining))) {
                return 0;
            }
            crc = pngCrc(crc, bytes);
            read += count;
        }
        const auto checksum = readImage(hit.offset + offset + 8 + length, 4).bytes;
        if (checksum.size() != 4
            || (crc ^ 0xffffffffU) != qFromBigEndian<quint32>(checksum.constData())) {
            return 0;
        }
        offset += quint64(length) + 12;
        hasData |= type == "IDAT" && length > 0;
        if (type == "IEND") {
            return !length && hasData ? offset : 0;
        }
    }
    return 0;
}
