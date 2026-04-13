
#include "TempConfig.h"
#include "core/Iaito.h"

TempConfig::~TempConfig()
{
    for (auto i = resetValues.constBegin(); i != resetValues.constEnd(); ++i) {
        Core()->setConfig(i.key(), i.value());
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
        resetValues[key] = Core()->getConfig(key);
    }

    Core()->setConfig(key, value);
    return *this;
}

TempConfig &TempConfig::set(const QString &key, bool value)
{
    if (!resetValues.contains(key)) {
        resetValues[key] = Core()->getConfig(key);
    }

    Core()->setConfig(key, value);
    return *this;
}
