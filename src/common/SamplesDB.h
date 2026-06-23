#ifndef IAITO_SAMPLESDB_H
#define IAITO_SAMPLESDB_H

#include <QList>
#include <QPair>
#include <QString>

// On-disk database mapping sha256 <-> absolute file path. Entries are sharded
// git/rainbow-table style across two hash-prefix directory levels, each leaf
// being a plain text file with "<sha256> <path>" lines. See doc/deeplinks.md.
namespace SamplesDB {

// Absolute path of the database root (lazily created).
QString dbRoot();

// Computes the lowercase hex sha256 of a file using radare2's incremental
// hashing API in fixed-size blocks (bounded memory for huge files). Returns an
// empty string on failure.
QString sha256File(const QString &path);

// Hashes path and stores the sha256 -> absolute path mapping (latest path wins).
// Idempotent. Returns the computed sha256, or an empty string on failure.
QString registerFile(const QString &path);

// Returns the path registered for sha256, or an empty string if unknown.
QString pathForHash(const QString &sha256);

// Removes the entry for sha256. Returns true if something was removed.
bool remove(const QString &sha256);

// Returns every {sha256, path} pair stored in the database.
QList<QPair<QString, QString>> all();

} // namespace SamplesDB

#endif // IAITO_SAMPLESDB_H
