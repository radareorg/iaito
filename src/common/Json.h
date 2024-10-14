
#ifndef IAITO_JSON_H
#define IAITO_JSON_H

#include "core/Iaito.h"

#include <QJsonValue>

static inline RVA JsonValueGetRVA(const QJsonValue &value, RVA defaultValue = RVA_INVALID)
{
    bool ok;
    RVA ret = value.toVariant().toULongLong(&ok);
    if (!ok) {
        return defaultValue;
    }
    return ret;
}

#endif // IAITO_JSON_H
