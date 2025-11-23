
#include "TempConfig.h"
#include "core/Iaito.h"

namespace {
static inline int variantTypeId(const QVariant &v)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return v.metaType().id();
#else
    return v.userType();
#endif
}
} // namespace

TempConfig::~TempConfig()
{
    for (auto i = resetValues.constBegin(); i != resetValues.constEnd(); ++i) {
        switch (variantTypeId(i.value())) {
        case QMetaType::QString:
            Core()->setConfig(i.key(), i.value().toString());
            break;
        case QMetaType::Int:
            Core()->setConfig(i.key(), i.value().toInt());
            break;
        case QMetaType::Bool:
            Core()->setConfig(i.key(), i.value().toBool());
            break;
        default:
            break;
        }
    }
}

TempConfig &TempConfig::set(const QString &key, const QString &value)
{
    if (!resetValues.contains(key)) {
        resetValues[key] = Core()->getConfig(key);
    }

    Core()->setConfig(key, value);
    return *this;
}

TempConfig &TempConfig::set(const QString &key, const char *value)
{
    if (!resetValues.contains(key)) {
        resetValues[key] = Core()->getConfig(key);
    }

    Core()->setConfig(key, value);
    return *this;
}

TempConfig &TempConfig::set(const QString &key, int value)
{
    if (!resetValues.contains(key)) {
        resetValues[key] = Core()->getConfigi(key);
    }

    Core()->setConfig(key, value);
    return *this;
}

TempConfig &TempConfig::set(const QString &key, bool value)
{
    if (!resetValues.contains(key)) {
        resetValues[key] = Core()->getConfigb(key);
    }

    Core()->setConfig(key, value);
    return *this;
}
