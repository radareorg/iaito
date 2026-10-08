#include "SourceLineReference.h"

QList<SourceLineReference> SourceLineReference::parse(const QString &contents)
{
    QList<SourceLineReference> result;
    for (const QString &row : contents.split(QLatin1Char('\n'))) {
        const int firstTab = row.indexOf(QLatin1Char('\t'));
        const int secondTab = row.indexOf(QLatin1Char('\t'), firstTab + 1);
        if (firstTab <= 0 || secondTab < 0) {
            continue;
        }
        const QString addressText = row.left(firstTab);
        if (!addressText.startsWith(QStringLiteral("0x"))) {
            continue;
        }
        bool addressOk = false;
        bool lineOk = false;
        const quint64 address = addressText.mid(2).toULongLong(&addressOk, 16);
        const int line = row.mid(firstTab + 1, secondTab - firstTab - 1).toInt(&lineOk);
        if (!addressOk || !lineOk || line < 0) {
            continue;
        }
        QString code = row.mid(secondTab + 1);
        if (code.endsWith(QLatin1Char('\r'))) {
            code.chop(1);
        }
        result.append({address, line, code});
    }
    return result;
}
