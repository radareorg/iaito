#include "core/FilesystemEvidence.h"
#include "core/Iaito.h"

#include <algorithm>
#include <cstring>
#include <QCoreApplication>
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
    const QByteArray uri = "file://" + QFile::encodeName(imagePath);
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
    if (!addPartitions("gpt")) {
        if (addPartitions("mbr") || addPartitions("msdos")) {
            addPartitions("ebr", true);
        } else if (!addPartitions("apm")) {
            addPartitions("apple");
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
                                 .arg(partition->number);
        addVolume(name, partition->start, partition->length);
    }
    return imageVolumes.size() != previous;
}

void FilesystemEvidence::addVolume(const QString &name, quint64 start, quint64 length)
{
    std::unique_ptr<char, decltype(&free)> detected(r_fs_name(fs, start), free);
    const QByteArray mount = "/volume" + QByteArray::number(imageVolumes.size());
    auto *root = detected ? r_fs_mount(fs, detected.get(), mount.constData(), start) : nullptr;
    Volume
        volume{name, QString::fromUtf8(detected.get()), root ? mount : QByteArray(), start, length};
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
    imageVolumes.append(volume);
}

QString FilesystemEvidence::volumeDetails(int index) const
{
    const auto &volume = imageVolumes.at(index);
    if (volume.mount.isEmpty()) {
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
    const auto &volume = imageVolumes.at(index);
    if (readable) {
        *readable = false;
    }
    if (volume.mount.isEmpty() || (view != DirectoryView::Live && !volume.deletedEntries)) {
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
    if (volume.mount.isEmpty()) {
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
        const bool cached = file->data;
        const int count = cached ? request : r_fs_read(fs, file, offset, request);
        if (file->data && count > 0 && count <= request) {
            const auto *data = reinterpret_cast<const char *>(file->data);
            page.bytes = QByteArray(data + (cached ? offset : 0), count);
        }
        page.error = page.bytes.size() != request;
    }
    r_fs_close(fs, file);
    r_fs_file_free(file);
    return page;
}

FilesystemEvidence::FilePage FilesystemEvidence::readImage(quint64 offset, int limit) const
{
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

FilesystemEvidence::Mapping FilesystemEvidence::fileMapping(
    int index, const Entry &entry, MappingMethod method) const
{
    Mapping mapping;
    mapping.declaredBytes = entry.size;
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
    const auto collect = [](const RFSRange *range, void *user) {
        auto &ranges = *static_cast<QList<Range> *>(user);
        ranges.append({range->logical, range->physical, range->size, range->flags});
        return true;
    };
    mapping.status = walkStatus(r_fs_extents(file, collect, &mapping.ranges));
    mapping.conflict = (file->flags & R_FS_FILE_CONFLICT) || entry.conflict;
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
#endif
    return mapping;
}

QStringList FilesystemEvidence::findOwners(int index, quint64 offset, bool deleted) const
{
    const auto &volume = imageVolumes.at(index);
    if (volume.mount.isEmpty() || offset < volume.start || offset - volume.start >= volume.length) {
        return {};
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
