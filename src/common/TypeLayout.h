#ifndef TYPELAYOUT_H
#define TYPELAYOUT_H

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QStringList>
#include <QVector>

// A snapshot of r2's type database. Keep stored offsets: pf formats and tsj can
// lose padding, and JSON numbers cannot represent every 64-bit type-link address.
class TypeLayout
{
public:
    struct Field
    {
        QString name;
        QString type;
        quint64 offset = 0;
        quint64 count = 1;
    };

    void load(const QString &database, int pointerSize);
    QStringList names() const;
    QMap<quint64, QString> links() const;
    QString resolve(QString type) const;
    QString kind(const QString &type) const;
    QVector<Field> fields(const QString &type) const;
    quint64 size(const QString &type) const;
    QString value(const QString &type, const QByteArray &bytes, bool bigEndian) const;
    static quint64 unsignedValue(const QByteArray &bytes, bool bigEndian);
    static bool array(const QString &type, QString *element, quint64 *count);

private:
    QHash<QString, QString> database;
    int pointerSize = 8;
    mutable QHash<QString, quint64> sizes;
    QVector<Field> fields(const QString &type, const QStringList &ancestors) const;
    quint64 size(const QString &type, QStringList ancestors) const;
};

#endif // TYPELAYOUT_H
