#include "core/FilesystemEvidence.h"
#include "core/Iaito.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace {
#if R2_ABIVERSION >= 188
FilesystemEvidence::WalkStatus walkStatus(int status)
{
    using Status = FilesystemEvidence::WalkStatus;
    switch (status) {
    case R_FS_WALK_COMPLETE:
        return Status::Complete;
    case R_FS_WALK_STOPPED:
        return Status::Stopped;
    case R_FS_WALK_UNSUPPORTED:
        return Status::Unsupported;
    default:
        return Status::Error;
    }
}
#endif
} // namespace

std::shared_ptr<FilesystemEvidence> IaitoCore::openFilesystemEvidence(
    const QString &path, QString *error)
{
    auto evidence = std::shared_ptr<FilesystemEvidence>(new FilesystemEvidence(path));
    return evidence->open(error) ? evidence : nullptr;
}

FilesystemEvidence::FilesystemEvidence(const QString &path)
    : imagePath(path)
{}

FilesystemEvidence::~FilesystemEvidence()
{
    r_fs_free(fs);
    r_io_free(io);
}

bool FilesystemEvidence::open(QString *error)
{
    auto fail = [error](const char *message) {
        if (error) {
            *error = QCoreApplication::translate("FilesystemEvidence", message);
        }
        return false;
    };
    const QFileInfo info(imagePath);
    if (!info.isFile()) {
        return fail(QT_TRANSLATE_NOOP("FilesystemEvidence", "Select a local disk image."));
    }
    imagePath = info.canonicalFilePath();
    io = r_io_new();
    fs = r_fs_new();
    if (!io || !fs) {
        return fail(QT_TRANSLATE_NOOP("FilesystemEvidence", "Cannot initialize filesystem I/O."));
    }
    io->va = false;
#ifdef Q_OS_WIN
    const QByteArray uri = "file://" + imagePath.toUtf8();
#else
    const QByteArray uri = "file://" + QFile::encodeName(imagePath);
#endif
    auto *descriptor = r_io_open(io, uri.constData(), R_PERM_R, 0);
    if (!descriptor) {
        return fail(QT_TRANSLATE_NOOP("FilesystemEvidence", "Cannot open the image read-only."));
    }
    imageSize = r_io_desc_size(descriptor);
    if (!imageSize || imageSize == UT64_MAX) {
        return fail(QT_TRANSLATE_NOOP("FilesystemEvidence", "Cannot determine the image size."));
    }
    r_io_bind(io, &fs->iob);
    discoverVolumes();
    return true;
}

void FilesystemEvidence::discoverVolumes()
{
    const auto header = readImage(0, 520).bytes;
    const auto signature = header.mid(512, 8);
    if (signature == "EFI PART") {
        addPartitions("gpt");
    } else if (signature.startsWith("PM")) {
        if (!addPartitions("apm")) {
            addPartitions("apple");
        }
    } else if (header.mid(510, 2) == QByteArray::fromHex("55aa")) {
        if (addPartitions("mbr") || addPartitions("msdos")) {
            addPartitions("ebr", true);
        }
    }
    if (imageVolumes.isEmpty()) {
        addVolume(QCoreApplication::translate("FilesystemEvidence", "Whole image"), 0, imageSize);
    }
    std::sort(imageVolumes.begin(), imageVolumes.end(), [](const Volume &a, const Volume &b) {
        return a.start < b.start;
    });
}

bool FilesystemEvidence::addPartitions(const char *scheme, bool logicalOnly)
{
    for (int i = 0;; ++i) {
        const char *available = r_fs_partition_type_get(i);
        if (!available) {
            return false;
        }
        if (std::strcmp(available, scheme) == 0) {
            break;
        }
    }
    std::unique_ptr<RList, decltype(&r_list_free)>
        partitions(r_fs_partitions(fs, scheme, 0), r_list_free);
    auto *list = partitions.get();
    const auto previous = imageVolumes.size();
    RListIter *iter;
    RFSPartition *partition;
    IaitoRListForeach(list, iter, RFSPartition, partition)
    {
        if (!partition->start || partition->start >= imageSize || !partition->length
            || partition->length > imageSize - partition->start) {
            continue;
        }
        if (logicalOnly
            && std::any_of(imageVolumes.cbegin(), imageVolumes.cend(), [partition](const Volume &v) {
                   return v.start == partition->start;
               })) {
            continue;
        }
        const QString name = QCoreApplication::translate("FilesystemEvidence", "%1 partition %2")
                                 .arg(QString::fromLatin1(scheme).toUpper())
                                 .arg(partition->number + 1);
        addVolume(name, partition->start, partition->length);
    }
    return imageVolumes.size() != previous;
}

void FilesystemEvidence::addVolume(const QString &name, quint64 start, quint64 length)
{
    const QByteArray mount = "/volume" + QByteArray::number(imageVolumes.size());
    Volume volume{name, {}, {}, start, length};
    const quint32 imageBank = io->bank;
    auto *bank = r_io_bank_new(mount.constData());
    if (!bank || !r_io_bank_add(io, bank)) {
        r_io_bank_free(bank);
        imageVolumes.append(volume);
        return;
    }
    volume.bank = bank->id;
    r_io_bank_use(io, volume.bank);
    io->va = true;
    const auto *map = r_io_map_add(io, io->desc->fd, R_PERM_R, start, start, length);
    std::unique_ptr<char, decltype(&free)> detected(map ? r_fs_name(fs, start) : nullptr, free);
    auto *root = detected ? r_fs_mount(fs, detected.get(), mount.constData(), start) : nullptr;
    volume.filesystem = QString::fromUtf8(detected.get());
    volume.mount = root ? mount : QByteArray();
#if R2_ABIVERSION >= 187
    if (root && root->p->open_id && volume.filesystem == QLatin1String("fat")) {
        volume.deletedEntries = true;
    } else if (root && root->p->open_id && volume.filesystem == QLatin1String("ext2")) {
        if (auto *directory = r_fs_open_id(fs, mount.constData(), 2, R_FS_OPEN_CHAIN)) {
            volume.deletedEntries = true;
            r_fs_close(fs, directory);
            r_fs_file_free(directory);
        }
    }
#endif
    r_io_bank_use(io, imageBank);
    io->va = false;
    imageVolumes.append(volume);
}

bool FilesystemEvidence::selectVolume(const Volume &volume) const
{
    if (volume.mount.isEmpty() || !r_io_bank_use(io, volume.bank)) {
        return false;
    }
    io->va = true;
    return true;
}

QString FilesystemEvidence::volumeDetails(int index) const
{
    const std::lock_guard lock(ioMutex);
    const auto &volume = imageVolumes.at(index);
    if (!selectVolume(volume)) {
        return {};
    }
    auto *roots = r_fs_root(fs, volume.mount.constData());
    auto *root = static_cast<RFSRoot *>(r_list_first(roots));
    RStrBuf *details = r_strbuf_new("");
    if (root && root->p->details) {
        root->p->details(root, details);
    }
    const QString result = QString::fromUtf8(r_strbuf_get(details));
    r_strbuf_free(details);
    r_list_free(roots);
    return result;
}

QList<FilesystemEvidence::Entry> FilesystemEvidence::listDirectory(
    int index, const QByteArray &path, bool *readable, DirectoryView view, quint64 directoryId) const
{
    const std::lock_guard lock(ioMutex);
    const auto &volume = imageVolumes.at(index);
    if (readable) {
        *readable = false;
    }
    if ((view != DirectoryView::Live && !volume.deletedEntries) || !selectVolume(volume)) {
        return {};
    }
    const QByteArray fullPath = volume.mount + path;
    RList *list;
#if R2_ABIVERSION >= 187
    if (view == DirectoryView::FreeInodes) {
        if (volume.filesystem != QLatin1String("ext2")) {
            return {};
        }
        list = r_fs_dir(fs, volume.mount.constData(), 0, R_FS_VIEW_DELETED);
    } else {
        int mask = R_FS_VIEW_NORMAL | R_FS_VIEW_SPECIAL;
        if (view != DirectoryView::Live) {
            mask |= R_FS_VIEW_DELETED;
        }
        list = r_fs_dir(fs, fullPath.constData(), directoryId, mask);
    }
#else
    Q_UNUSED(directoryId);
    list = iaitoFilesystemDirectory(fs, fullPath.constData());
#endif
    const std::unique_ptr<RList, decltype(&r_list_free)> files(list, r_list_free);
    if (readable) {
        *readable = bool(files);
    }
    QList<Entry> entries;
    RListIter *iter;
    RFSFile *file;
    IaitoRListForeach(list, iter, RFSFile, file)
    {
        const QByteArray name(file->name);
        if (name.isEmpty() || name == "." || name == ".." || name.contains('/')) {
            continue;
        }
        Entry entry;
        entry.name = QString::fromUtf8(name);
        entry.path = path + '/' + name;
        entry.size = file->size;
        entry.type = file->type;
        entry.directory = file->type == R_FS_FILE_TYPE_DIRECTORY;
        entry.modified = file->time;
        entry.owner = file->uid;
        entry.group = file->gid;
        entry.mode = file->perm;
#if R2_ABIVERSION >= 187
        entry.id = file->id;
        entry.firstOffset = file->off;
        entry.deleted = file->flags & R_FS_FILE_DELETED;
        entry.inferredName = file->flags & R_FS_FILE_HEURISTIC;
        entry.conflict = file->flags & R_FS_FILE_CONFLICT;
        if (view == DirectoryView::Deleted && !entry.deleted && !entry.directory) {
            continue;
        }
#endif
        entries.append(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.directory != b.directory ? a.directory : a.name.localeAwareCompare(b.name) < 0;
    });
    return entries;
}

RFSFile *FilesystemEvidence::openEntry(int index, const Entry &entry, MappingMethod method) const
{
    const auto &volume = imageVolumes.at(index);
    if (!selectVolume(volume)) {
        return nullptr;
    }
    if (entry.deleted) {
#if R2_ABIVERSION >= 188
        if (entry.id != UT64_MAX) {
            const ut32 flags = method == MappingMethod::Metadata ? R_FS_OPEN_CHAIN
                               : method == MappingMethod::Contiguous
                                   ? R_FS_OPEN_RECOVER | R_FS_OPEN_CONTIGUOUS
                                   : R_FS_OPEN_RECOVER;
            return r_fs_open_id(fs, volume.mount.constData(), entry.id, flags);
        }
#endif
        return nullptr;
    }
    Q_UNUSED(method);
    const QByteArray path = volume.mount + entry.path;
    return r_fs_open(fs, path.constData(), false);
}

FilesystemEvidence::FilePage FilesystemEvidence::readFile(
    int index, const Entry &entry, quint64 offset, int limit) const
{
    const std::lock_guard lock(ioMutex);
    FilePage page;
    if (limit <= 0 || entry.directory) {
        return page;
    }
    auto *file = openEntry(index, entry, MappingMethod::Retained);
    if (!file) {
        return page;
    }
    page.opened = true;
    page.size = file->size;
    if (offset < page.size) {
        const auto request = int(qMin<quint64>(limit, page.size - offset));
        page.bytes = readOpenFile(file, offset, request, file->data != nullptr);
        page.error = page.bytes.size() != request;
    }
    r_fs_close(fs, file);
    r_fs_file_free(file);
    return page;
}

QByteArray FilesystemEvidence::readOpenFile(
    RFSFile *file, quint64 offset, int request, bool cached) const
{
    const int count = cached ? request : r_fs_read(fs, file, offset, request);
    if (!file->data || count <= 0 || count > request) {
        return {};
    }
    const auto *data = reinterpret_cast<const char *>(file->data);
    return QByteArray(data + (cached ? offset : 0), count);
}

FilesystemEvidence::FilePage FilesystemEvidence::readImage(quint64 offset, int limit) const
{
    const std::lock_guard lock(ioMutex);
    FilePage page;
    page.opened = true;
    page.size = imageSize;
    if (limit > 0 && offset < imageSize) {
        const int length = int(qMin<quint64>(limit, imageSize - offset));
        page.bytes.resize(length);
        const int count
            = r_io_pread_at(io, offset, reinterpret_cast<ut8 *>(page.bytes.data()), length);
        page.error = count != length;
        page.bytes.resize(qBound(0, count, length));
    }
    return page;
}

QByteArray FilesystemEvidence::imageSha256(const Progress &progress) const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (quint64 offset = 0; offset < imageSize;) {
        if (progress && !progress(offset, imageSize)) {
            return {};
        }
        const int length = int(qMin<quint64>(1024 * 1024, imageSize - offset));
        const auto page = readImage(offset, length);
        if (page.error || page.bytes.size() != length) {
            return {};
        }
        hash.addData(page.bytes);
        offset += length;
    }
    return hash.result().toHex();
}

FilesystemEvidence::Mapping FilesystemEvidence::fileMapping(
    int index, const Entry &entry, MappingMethod method, const Progress &progress) const
{
    const std::lock_guard lock(ioMutex);
    Mapping mapping;
    mapping.declaredBytes = entry.size;
    mapping.conflict = entry.conflict;
    mapping.heuristic = method == MappingMethod::Contiguous && entry.deleted;
#if R2_ABIVERSION >= 188
    const auto &volume = imageVolumes.at(index);
    if (volume.mount.isEmpty() || !volume.deletedEntries) {
        return mapping;
    }
    auto *file = openEntry(index, entry, method);
    if (!file) {
        mapping.status = WalkStatus::Error;
        return mapping;
    }
    mapping.declaredBytes = file->size;
    struct Extents
    {
        const Volume &volume;
        QList<Range> &ranges;
        const Progress &progress;
        quint64 declared;
        bool outsideVolume = false;
    } extents{volume, mapping.ranges, progress, mapping.declaredBytes};
    const auto collect = [](const RFSRange *range, void *user) {
        auto &extents = *static_cast<Extents *>(user);
        const auto &volume = extents.volume;
        if (extents.progress
            && !extents.progress(qMin(range->logical, extents.declared), extents.declared)) {
            return false;
        }
        if (range->physical != UT64_MAX
            && (range->physical < volume.start || range->physical - volume.start >= volume.length
                || range->size > volume.length - (range->physical - volume.start))) {
            extents.outsideVolume = true;
            return false;
        }
        extents.ranges.append({range->logical, range->physical, range->size, range->flags});
        return true;
    };
    const int status = r_fs_extents(file, collect, &extents);
    mapping.status = extents.outsideVolume ? WalkStatus::Error : walkStatus(status);
    mapping.conflict |= (file->flags & R_FS_FILE_CONFLICT) != 0;
    mapping.partial = file->flags & R_FS_FILE_PARTIAL;
    mapping.heuristic
        = method == MappingMethod::Contiguous
          || std::any_of(mapping.ranges.cbegin(), mapping.ranges.cend(), [](const Range &range) {
                 return range.flags & Heuristic;
             });
    r_fs_close(fs, file);
    r_fs_file_free(file);
#else
    Q_UNUSED(index);
    Q_UNUSED(method);
    Q_UNUSED(progress);
#endif
    return mapping;
}

QStringList FilesystemEvidence::findOwners(int index, quint64 offset, bool deleted) const
{
    const std::lock_guard lock(ioMutex);
    const auto &volume = imageVolumes.at(index);
    if (offset < volume.start || offset - volume.start >= volume.length || !selectVolume(volume)) {
        return {};
    }
    std::optional<RCoreLocked> legacy;
    if (!volume.deletedEntries) {
        legacy.emplace(Core());
    }
#if R2_ABIVERSION >= 187
    RList *list = r_fs_find_off(
        fs, volume.mount.constData(), offset, deleted ? R_FS_VIEW_DELETED : R_FS_VIEW_NORMAL);
#else
    RList *list = deleted ? nullptr : r_fs_find_off(fs, volume.mount.constData(), offset);
#endif
    const std::unique_ptr<RList, decltype(&r_list_free)> paths(list, r_list_free);
    QStringList owners;
    RListIter *iter;
    const char *path;
    IaitoRListForeach(list, iter, const char, path)
    {
        const QByteArray fullPath(path);
        if (fullPath.startsWith(volume.mount + '/')) {
            owners.append(QString::fromUtf8(fullPath.mid(volume.mount.size())));
        }
    }
    owners.removeDuplicates();
    return owners;
}

FilesystemEvidence::Mapping FilesystemEvidence::allocationMapping(
    int index, quint32 states, const Progress &progress) const
{
    const std::lock_guard lock(ioMutex);
    Mapping mapping;
#if R2_ABIVERSION >= 188
    const auto &volume = imageVolumes.at(index);
    if (!volume.deletedEntries || !selectVolume(volume)) {
        return mapping;
    }
    struct Scan
    {
        const Volume &volume;
        QList<Range> &ranges;
        const Progress &progress;
        bool outsideVolume = false;
    } scan{volume, mapping.ranges, progress};
    const auto collect = [](const RFSRange *range, void *user) {
        auto &scan = *static_cast<Scan *>(user);
        if (range->physical < scan.volume.start || !range->size) {
            scan.outsideVolume = true;
            return false;
        }
        const quint64 relative = range->physical - scan.volume.start;
        if (relative >= scan.volume.length || range->size > scan.volume.length - relative) {
            scan.outsideVolume = true;
            return false;
        }
        if (scan.progress && !scan.progress(relative + range->size, scan.volume.length)) {
            return false;
        }
        scan.ranges.append({range->logical, range->physical, range->size, range->flags});
        return true;
    };
    const int status = r_fs_ranges(fs, volume.mount.constData(), states, collect, &scan);
    mapping.status = scan.outsideVolume ? WalkStatus::Error : walkStatus(status);
    mapping.declaredBytes = volume.length;
#else
    Q_UNUSED(index);
    Q_UNUSED(states);
    Q_UNUSED(progress);
#endif
    return mapping;
}

FilesystemEvidence::Transfer FilesystemEvidence::exportFile(
    int index,
    const Entry &entry,
    MappingMethod method,
    QIODevice &output,
    const Progress &progress) const
{
    const std::lock_guard lock(ioMutex);
    Transfer result;
    result.declaredBytes = entry.size;
    result.heuristic = method == MappingMethod::Contiguous && entry.deleted;
    if (entry.directory || method == MappingMethod::Metadata || !output.isWritable()) {
        result.error
            = QCoreApplication::translate("FilesystemEvidence", "Cannot export this entry.");
        return result;
    }

    // Legacy readers can share global GRUB I/O state with the main core.
    const bool legacy = !imageVolumes.at(index).deletedEntries;
    auto lockLegacy = [legacy]() -> std::optional<RCoreLocked> {
        return legacy ? std::optional<RCoreLocked>(std::in_place, Core()) : std::nullopt;
    };
    RFSFile *opened;
    {
        const auto lock = lockLegacy();
        opened = openEntry(index, entry, method);
    }
    const auto closeFile = [this, &lockLegacy](RFSFile *file) {
        const auto lock = lockLegacy();
        r_fs_close(fs, file);
        r_fs_file_free(file);
    };
    const std::unique_ptr<RFSFile, decltype(closeFile)> file(opened, closeFile);
    if (!file) {
        result.error = QCoreApplication::translate("FilesystemEvidence", "Cannot open the entry.");
        return result;
    }

    result.declaredBytes = file->size;
    quint64 available = file->size;
    bool completeMap = true;
#if R2_ABIVERSION >= 188
    if (entry.deleted) {
        struct Plan
        {
            const Progress &progress;
            quint64 readable = 0;
            quint64 declared = 0;
            bool heuristic = false;
        } plan{progress, 0, file->size};
        const auto collect = [](const RFSRange *range, void *user) {
            auto &plan = *static_cast<Plan *>(user);
            if ((plan.progress && !plan.progress(0, 0)) || range->logical != plan.readable
                || range->size > plan.declared - plan.readable) {
                return false;
            }
            plan.readable += range->size;
            plan.heuristic |= (range->flags & R_FS_RANGE_HEURISTIC) != 0;
            return true;
        };
        const int status = r_fs_extents(file.get(), collect, &plan);
        available = plan.readable;
        completeMap = status == R_FS_WALK_COMPLETE && available == file->size;
        result.conflict = (file->flags & R_FS_FILE_CONFLICT) != 0;
        result.heuristic |= plan.heuristic;
    }
#endif
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const bool cached = file->data != nullptr;
    while (result.writtenBytes < available) {
        if (progress && !progress(result.writtenBytes, available)) {
            result.cancelled = true;
            break;
        }
        const int length = int(qMin<quint64>(256 * 1024, available - result.writtenBytes));
        QByteArray bytes;
        {
            const auto lock = lockLegacy();
            bytes = readOpenFile(file.get(), result.writtenBytes, length, cached);
        }
        if (bytes.isEmpty()) {
            break;
        }
        if (output.write(bytes) != bytes.size()) {
            result.error = output.errorString();
            result.writeError = true;
            break;
        }
        hash.addData(bytes);
        result.writtenBytes += quint64(bytes.size());
    }
    result.cancelled |= progress && !progress(result.writtenBytes, available);
    result.conflict |= entry.conflict;
    result.complete = !result.cancelled && !result.writeError && completeMap
                      && result.writtenBytes == result.declaredBytes;
    if (result.complete || result.writtenBytes) {
        result.sha256 = hash.result().toHex();
    }
    if (!result.complete && !result.cancelled && result.error.isEmpty()) {
        result.error
            = result.conflict
                  ? QCoreApplication::translate(
                        "FilesystemEvidence", "The data range has a live owner.")
                  : QCoreApplication::translate("FilesystemEvidence", "The file data is incomplete.");
    }
    return result;
}

FilesystemEvidence::Transfer FilesystemEvidence::exportImageRange(
    quint64 offset, quint64 length, QIODevice &output, const Progress &progress) const
{
    Transfer result;
    result.declaredBytes = length;
    if (offset >= imageSize || length > imageSize - offset || !output.isWritable()) {
        result.error = QCoreApplication::translate("FilesystemEvidence", "Invalid export range.");
        return result;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (result.writtenBytes < length) {
        if (progress && !progress(result.writtenBytes, length)) {
            result.cancelled = true;
            return result;
        }
        const int count = int(qMin<quint64>(256 * 1024, length - result.writtenBytes));
        const auto page = readImage(offset + result.writtenBytes, count);
        if (page.error || page.bytes.size() != count) {
            result.error
                = QCoreApplication::translate("FilesystemEvidence", "Cannot read the range.");
            return result;
        }
        if (output.write(page.bytes) != count) {
            result.error = output.errorString();
            result.writeError = true;
            return result;
        }
        hash.addData(page.bytes);
        result.writtenBytes += count;
    }
    result.complete = true;
    result.sha256 = hash.result().toHex();
    return result;
}
