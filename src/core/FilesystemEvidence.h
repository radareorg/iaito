#pragma once

#include "core/IaitoCommon.h"

#include <memory>
#include <QList>
#include <QMetaType>

class IaitoCore;

class FilesystemEvidence
{
public:
    enum class DirectoryView { Live, All, Deleted, FreeInodes };

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
        bool deleted = false;
        bool inferredName = false;
        bool conflict = false;
    };

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

private:
    friend class IaitoCore;
    explicit FilesystemEvidence(const QString &path);
    bool open(QString *error);
    void discoverVolumes();
    bool addPartitions(const char *scheme, bool logicalOnly = false);
    void addVolume(const QString &name, quint64 start, quint64 length);

    QString imagePath;
    quint64 imageSize = 0;
    QList<Volume> imageVolumes;
    RIO *io = nullptr;
    RFS *fs = nullptr;
};

Q_DECLARE_METATYPE(FilesystemEvidence::Entry)
