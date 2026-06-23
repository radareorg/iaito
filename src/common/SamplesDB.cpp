#include "SamplesDB.h"

#include <r_hash.h>
#include <r_util.h>

#include <atomic>

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QRunnable>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>

namespace {

// Heap-allocated and never freed: a process-lifetime mutex so a background
// worker that is still finishing during shutdown can never lock a destroyed
// object (function-local statics are destroyed at exit).
QMutex &dbMutex()
{
    static QMutex *m = new QMutex();
    return *m;
}

QThreadPool &pool()
{
    static QThreadPool *p = [] {
        auto *tp = new QThreadPool();
        tp->setMaxThreadCount(2);
        return tp;
    }();
    return *p;
}

std::atomic<bool> g_shutdown{false};

class FnRunnable : public QRunnable
{
public:
    explicit FnRunnable(std::function<void()> fn)
        : m_fn(std::move(fn))
    {
        setAutoDelete(true);
    }
    void run() override { m_fn(); }

private:
    std::function<void()> m_fn;
};

bool isValidSha256(const QString &h)
{
    if (h.size() != 64) {
        return false;
    }
    for (const QChar &c : h) {
        if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
              || (c >= QLatin1Char('a') && c <= QLatin1Char('f')))) {
            return false;
        }
    }
    return true;
}

// samples/<h0:2>/<h2:4>/<h0:4> bucket file holding "<sha256> <path>" lines.
QString bucketFile(const QString &h)
{
    return QStringLiteral("%1/%2/%3/%4")
        .arg(SamplesDB::dbRoot(), h.mid(0, 2), h.mid(2, 2), h.mid(0, 4));
}

QList<QPair<QString, QString>> readBucketFile(const QString &file)
{
    QList<QPair<QString, QString>> out;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &raw : lines) {
        QString line = QString::fromUtf8(raw);
        // Strip only the line terminator; paths may legitimately end in spaces.
        while (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        const int sp = line.indexOf(QLatin1Char(' '));
        if (sp <= 0) {
            continue;
        }
        const QString path = line.mid(sp + 1);
        if (!path.isEmpty()) {
            out.append({line.left(sp), path});
        }
    }
    return out;
}

bool writeBucketFile(const QString &file, const QList<QPair<QString, QString>> &entries)
{
    if (entries.isEmpty()) {
        return QFile::remove(file) || !QFile::exists(file);
    }
    QDir().mkpath(QFileInfo(file).absolutePath());
    // QSaveFile writes to a temp file and atomically renames over the target on
    // commit, so the live bucket is never truncated mid-write.
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    QByteArray data;
    for (const auto &e : entries) {
        data += e.first.toUtf8();
        data += ' ';
        data += e.second.toUtf8();
        data += '\n';
    }
    if (f.write(data) != data.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

} // namespace

namespace SamplesDB {

QString dbRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("samples"));
}

QString sha256File(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QString();
    }
    RHash *ctx = r_hash_new(true, R_HASH_SHA256);
    if (!ctx) {
        return QString();
    }
    // do_begin sets rst=false so subsequent blocks accumulate instead of each
    // one re-initialising and finalising. Skipping it would hash only the last
    // block.
    r_hash_do_begin(ctx, R_HASH_SHA256);
    const qint64 blockSize = 1024 * 1024;
    QByteArray buf(blockSize, Qt::Uninitialized);
    bool ok = true;
    qint64 n;
    while ((n = f.read(buf.data(), blockSize)) > 0) {
        // Abort promptly on shutdown so a huge in-flight hash can't stall exit.
        if (g_shutdown.load()) {
            ok = false;
            break;
        }
        r_hash_calculate(ctx, R_HASH_SHA256, reinterpret_cast<const ut8 *>(buf.constData()), (int) n);
    }
    if (n < 0) {
        ok = false;
    }
    r_hash_do_end(ctx, R_HASH_SHA256);
    char hex[R_HASH_SIZE_SHA256 * 2 + 1];
    r_hex_bin2str(ctx->digest, R_HASH_SIZE_SHA256, hex);
    r_hash_free(ctx);
    return ok ? QString::fromLatin1(hex) : QString();
}

QString registerFile(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        return QString();
    }
    const QString abs = fi.absoluteFilePath();
    if (abs.contains(QLatin1Char('\n')) || abs.contains(QLatin1Char('\r'))) {
        return QString();
    }
    const QString h = sha256File(abs);
    if (!isValidSha256(h)) {
        return QString();
    }

    QMutexLocker lock(&dbMutex());
    const QString file = bucketFile(h);
    auto entries = readBucketFile(file);
    bool found = false;
    for (auto &e : entries) {
        if (e.first == h) {
            if (e.second == abs) {
                return h;
            }
            e.second = abs;
            found = true;
            break;
        }
    }
    if (!found) {
        entries.append({h, abs});
    }
    return writeBucketFile(file, entries) ? h : QString();
}

void registerFileAsync(const QString &path)
{
    if (g_shutdown.load()) {
        return;
    }
    const QString p = path;
    pool().start(new FnRunnable([p]() { registerFile(p); }));
}

void scanFolderAsync(const QString &dir, std::function<void()> onComplete)
{
    if (g_shutdown.load()) {
        return;
    }
    const QString d = dir;
    pool().start(new FnRunnable([d, onComplete]() {
        const QString rootPrefix = dbRoot() + QLatin1Char('/');
        QDirIterator it(d, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext() && !g_shutdown.load()) {
            const QString f = it.next();
            // Never register the database's own bucket files.
            if (QFileInfo(f).absoluteFilePath().startsWith(rootPrefix)) {
                continue;
            }
            registerFile(f);
        }
        if (onComplete) {
            onComplete();
        }
    }));
}

void waitForPending()
{
    g_shutdown.store(true);
    pool().clear();
    pool().waitForDone();
}

QString pathForHash(const QString &sha256)
{
    const QString h = sha256.toLower();
    if (!isValidSha256(h)) {
        return QString();
    }
    QMutexLocker lock(&dbMutex());
    const auto entries = readBucketFile(bucketFile(h));
    for (const auto &e : entries) {
        if (e.first == h) {
            return e.second;
        }
    }
    return QString();
}

bool remove(const QString &sha256)
{
    const QString h = sha256.toLower();
    if (!isValidSha256(h)) {
        return false;
    }
    QMutexLocker lock(&dbMutex());
    const QString file = bucketFile(h);
    const auto entries = readBucketFile(file);
    QList<QPair<QString, QString>> kept;
    for (const auto &e : entries) {
        if (e.first != h) {
            kept.append(e);
        }
    }
    if (kept.size() == entries.size()) {
        return false;
    }
    return writeBucketFile(file, kept);
}

QList<QPair<QString, QString>> all()
{
    QList<QPair<QString, QString>> out;
    QMutexLocker lock(&dbMutex());
    QDirIterator it(dbRoot(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        out += readBucketFile(it.next());
    }
    return out;
}

} // namespace SamplesDB
