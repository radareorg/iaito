#include "TypeLayout.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <QRegularExpression>

void TypeLayout::load(const QString &text, int pointerByteSize)
{
    database.clear();
    sizes.clear();
    pointerSize = qBound(1, pointerByteSize, 8);
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const int separator = line.indexOf(QLatin1Char('='));
        if (separator > 0) {
            database.insert(line.left(separator), line.mid(separator + 1));
        }
    }
}

QStringList TypeLayout::names() const
{
    QStringList result;
    for (auto it = database.cbegin(); it != database.cend(); ++it) {
        if (it.value() == QLatin1String("struct") || it.value() == QLatin1String("union")
            || it.value() == QLatin1String("typedef") || it.value() == QLatin1String("type")
            || it.value() == QLatin1String("enum")) {
            result.append(it.key());
        }
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QMap<quint64, QString> TypeLayout::links() const
{
    QMap<quint64, QString> result;
    for (auto it = database.cbegin(); it != database.cend(); ++it) {
        if (it.key().startsWith(QLatin1String("link."))) {
            bool ok = false;
            const quint64 address = it.key().mid(5).toULongLong(&ok, 16);
            if (ok) {
                result.insert(address, it.value());
            }
        }
    }
    return result;
}

QString TypeLayout::resolve(QString type) const
{
    static const QRegularExpression qualifiers(QStringLiteral("\\b(const|volatile|restrict)\\b"));
    static const QRegularExpression tag(QStringLiteral("^(struct|union|enum)\\s+"));
    QStringList visited;
    for (int i = 0; i < 32; ++i) {
        type.remove(qualifiers);
        type = type.simplified();
        type.remove(tag);
        if (visited.contains(type)) {
            return QString();
        }
        visited.append(type);
        if (database.value(type) != QLatin1String("typedef")) {
            return type;
        }
        type = database.value(QStringLiteral("typedef.") + type);
    }
    return QString();
}

bool TypeLayout::array(const QString &type, QString *element, quint64 *count)
{
    static const QRegularExpression expression(QStringLiteral("^(.*?)\\[(\\d+)\\](.*)$"));
    const auto match = expression.match(type);
    if (!match.hasMatch()) {
        return false;
    }
    bool ok = false;
    *count = match.captured(2).toULongLong(&ok);
    *element = (match.captured(1) + match.captured(3)).trimmed();
    return ok;
}

QString TypeLayout::kind(const QString &type) const
{
    const QString resolved = resolve(type);
    if (resolved.endsWith(QLatin1Char('*'))) {
        return QStringLiteral("pointer");
    }
    QString element;
    quint64 count;
    if (array(resolved, &element, &count)) {
        return QStringLiteral("array");
    }
    return database.value(resolved);
}

QVector<TypeLayout::Field> TypeLayout::fields(const QString &type) const
{
    return fields(type, {resolve(type)});
}

QVector<TypeLayout::Field> TypeLayout::fields(const QString &type, const QStringList &ancestors) const
{
    const QString resolved = resolve(type);
    const QString category = kind(resolved);
    QVector<Field> result;
    if (category != QLatin1String("struct") && category != QLatin1String("union")) {
        return result;
    }
    const QString prefix = category + QLatin1Char('.') + resolved;
    const QStringList members = database.value(prefix).split(QLatin1Char(','), Qt::SkipEmptyParts);
    bool allZero = true;
    for (const QString &name : members) {
        const QStringList details
            = database.value(prefix + QLatin1Char('.') + name).split(QLatin1Char(','));
        if (details.size() < 3) {
            continue;
        }
        bool ok = false;
        const quint64 offset = details.at(1).toULongLong(&ok, 0);
        if (!ok) {
            continue;
        }
        const quint64 count = details.at(2).toULongLong(&ok, 0);
        if (!ok) {
            continue;
        }
        result.append({name, details.first(), offset, qMax<quint64>(1, count)});
        allZero &= offset == 0;
    }
    // Some bundled r2 definitions omit offsets entirely. Their members are packed.
    if (allZero && category == QLatin1String("struct") && result.size() > 1) {
        quint64 offset = 0;
        for (Field &field : result) {
            field.offset = offset;
            const quint64 bytes = size(field.type, ancestors);
            if (bytes && field.count <= (std::numeric_limits<quint64>::max() - offset) / bytes) {
                offset += bytes * field.count;
            }
        }
    }
    return result;
}

quint64 TypeLayout::size(const QString &type) const
{
    const auto cached = sizes.constFind(type);
    if (cached != sizes.cend()) {
        return cached.value();
    }
    const quint64 result = size(type, {});
    sizes.insert(type, result);
    return result;
}

quint64 TypeLayout::size(const QString &type, QStringList ancestors) const
{
    const QString resolved = resolve(type);
    if (resolved.isEmpty() || ancestors.size() >= 32 || ancestors.contains(resolved)) {
        return 0;
    }
    ancestors.append(resolved);
    const QString category = kind(resolved);
    if (category == QLatin1String("pointer")) {
        return pointerSize;
    }
    QString element;
    quint64 count;
    if (array(resolved, &element, &count)) {
        const quint64 stride = size(element, ancestors);
        return stride && count <= std::numeric_limits<quint64>::max() / stride ? stride * count : 0;
    }
    if (category == QLatin1String("enum")) {
        return 4;
    }
    if (category == QLatin1String("struct") || category == QLatin1String("union")) {
        quint64 end = 0;
        for (const Field &field : fields(resolved, ancestors)) {
            const quint64 stride = size(field.type, ancestors);
            if (!stride
                || field.count > (std::numeric_limits<quint64>::max() - field.offset) / stride) {
                return 0;
            }
            end = qMax(end, field.offset + stride * field.count);
        }
        return end;
    }
    bool ok = false;
    const qint64 bits = database.value(QStringLiteral("type.") + resolved + QStringLiteral(".size"))
                            .toLongLong(&ok);
    return ok && bits > 0 ? quint64(bits) / 8 : 0;
}

quint64 TypeLayout::unsignedValue(const QByteArray &bytes, bool bigEndian)
{
    quint64 result = 0;
    for (int i = 0; i < qMin(8, int(bytes.size())); ++i) {
        const int shift = (bigEndian ? bytes.size() - i - 1 : i) * 8;
        if (shift < 64) {
            result |= quint64(static_cast<unsigned char>(bytes.at(i))) << shift;
        }
    }
    return result;
}

QString TypeLayout::value(const QString &type, const QByteArray &bytes, bool bigEndian) const
{
    const QString resolved = resolve(type);
    const quint64 number = unsignedValue(bytes, bigEndian);
    if (kind(resolved) == QLatin1String("pointer")) {
        return QStringLiteral("0x%1").arg(number, 0, 16);
    }
    if (resolved == QLatin1String("float") && bytes.size() == 4) {
        const quint32 raw = quint32(number);
        float value;
        memcpy(&value, &raw, sizeof(value));
        return QString::number(value, 'g', 8);
    }
    if (resolved == QLatin1String("double") && bytes.size() == 8) {
        double value;
        memcpy(&value, &number, sizeof(value));
        return QString::number(value, 'g', 16);
    }
    if (bytes.size() > 8) {
        return QString::fromLatin1(bytes.toHex(' '));
    }
    const bool isUnsigned = resolved.startsWith(QLatin1String("uint"))
                            || resolved.startsWith(QLatin1String("unsigned"));
    quint64 extended = number;
    if (!isUnsigned && !bytes.isEmpty() && bytes.size() < 8
        && (number & (quint64(1) << (bytes.size() * 8 - 1)))) {
        extended |= std::numeric_limits<quint64>::max() << (bytes.size() * 8);
    }
    qint64 signedNumber;
    memcpy(&signedNumber, &extended, sizeof(signedNumber));
    QString result = isUnsigned ? QString::number(number) : QString::number(signedNumber);
    if (kind(resolved) == QLatin1String("enum")) {
        const QString prefix = QStringLiteral("enum.") + resolved + QLatin1Char('.');
        for (auto it = database.cbegin(); it != database.cend(); ++it) {
            bool ok = false;
            const qint64 candidate = it.value().toLongLong(&ok, 0);
            if (it.key().startsWith(prefix) && ok && candidate == signedNumber) {
                result += QStringLiteral(" (%1)").arg(it.key().mid(prefix.size()));
                break;
            }
        }
    }
    if (resolved == QLatin1String("char") && number >= 32 && number < 127) {
        result += QStringLiteral(" '%1'").arg(QChar(ushort(number)));
    }
    return result + QStringLiteral(" (0x%1)").arg(number, 0, 16);
}
