#include "SamplesDB.h"

#include <r_hash.h>
#include <r_util.h>

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>

namespace {

QMutex &dbMutex()
{
    static QMutex m;
    return m;
}

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
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
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
        QFile::remove(file);
        return true;
    }
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    QByteArray data;
    for (const auto &e : entries) {
        data += e.first.toUtf8();
        data += ' ';
        data += e.second.toUtf8();
        data += '\n';
    }
    return f.write(data) == data.size();
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
