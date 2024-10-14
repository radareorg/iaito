#ifndef IAITO_LAYOUT_H
#define IAITO_LAYOUT_H

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVariantMap>

namespace Iaito {

struct IaitoLayout
{
    QByteArray geometry;
    QByteArray state;
    QMap<QString, QVariantMap> viewProperties;
};

const QString LAYOUT_DEFAULT = "Default";
const QString LAYOUT_DEBUG = "Debug";

bool isBuiltinLayoutName(const QString &name);

} // namespace Iaito
#endif
