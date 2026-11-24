#pragma once

#include "core/IaitoCommon.h"

#include <functional>
#include <memory>
#include <QList>
#include <QMetaType>

class IaitoCore;
class QIODevice;

class FilesystemEvidence
{
public:
    enum class DirectoryView { Live, All, Deleted, FreeInodes };
    enum class MappingMethod { Metadata, Retained, Contiguous };
    enum class WalkStatus { Unsupported, Error, Stopped, Complete };
    enum RangeFlag {
        Free = 1,
        Allocated = 2,
        Orphan = 4,
        Bad = 8,
        Unknown = 16,
        Heuristic = 32,
        Sparse = 64,
        Unwritten = 128,
    };

    struct Range
    {
        quint64 logical;
        quint64 physical;
        quint64 length;
        quint32 flags;
    };

    struct Mapping
    {
        QList<Range> ranges;
        WalkStatus status = WalkStatus::Unsupported;
        quint64 declaredBytes = 0;
        bool partial = false;
        bool conflict = false;
        bool heuristic = false;
    };

    struct Volume
    {
        QString partition;
        QString filesystem;
        QByteArray mount;
        quint64 start;
        quint64 length;
        bool deletedEntries = false;
    };

    struct Entry
    {
        QString name;
        QByteArray path;
        quint64 size = 0;
        bool directory = false;
        char type = 0;
        quint64 id = UT64_MAX;
        quint64 firstOffset = UT64_MAX;
        quint64 modified = 0;
        quint32 owner = 0;
        quint32 group = 0;
        quint32 mode = 0;
        bool deleted = false;
        bool inferredName = false;
        bool conflict = false;
    };

    struct FilePage
    {
        QByteArray bytes;
        quint64 size = 0;
        bool opened = false;
        bool error = false;
    };

    struct Transfer
    {
        quint64 declaredBytes = 0;
        quint64 writtenBytes = 0;
        QByteArray sha256;
        QString error;
        bool complete = false;
        bool conflict = false;
        bool heuristic = false;
        bool cancelled = false;
        bool writeError = false;
    };

    using Progress = std::function<bool(quint64 done, quint64 total)>;

    ~FilesystemEvidence();
    const QString &path() const { return imagePath; }
    quint64 size() const { return imageSize; }
    const QList<Volume> &volumes() const { return imageVolumes; }
    QString volumeDetails(int index) const;
    QList<Entry> listDirectory(
        int index,
        const QByteArray &path,
        bool *readable,
        DirectoryView view = DirectoryView::Live,
        quint64 directoryId = UT64_MAX) const;
    FilePage readFile(int index, const Entry &entry, quint64 offset, int limit) const;
    FilePage readImage(quint64 offset, int limit) const;
    Mapping fileMapping(int index, const Entry &entry, MappingMethod method) const;
    QStringList findOwners(int index, quint64 offset, bool deleted) const;
    Transfer exportFile(
        int index,
        const Entry &entry,
        MappingMethod method,
        QIODevice &output,
        const Progress &progress = {}) const;

private:
    friend class IaitoCore;
    explicit FilesystemEvidence(const QString &path);
    bool open(QString *error);
    void discoverVolumes();
    bool addPartitions(const char *scheme, bool logicalOnly = false);
    void addVolume(const QString &name, quint64 start, quint64 length);
    RFSFile *openEntry(int index, const Entry &entry, MappingMethod method) const;
    QByteArray readOpenFile(RFSFile *file, quint64 offset, int request, bool cached) const;

    QString imagePath;
    quint64 imageSize = 0;
    QList<Volume> imageVolumes;
    RIO *io = nullptr;
    RFS *fs = nullptr;
};

Q_DECLARE_METATYPE(FilesystemEvidence::Entry)
