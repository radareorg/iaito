#ifndef COMMON_SETTINGS_UPGRADE_H
#define COMMON_SETTINGS_UPGRADE_H

#include <QSettings>
#include <core/Iaito.h>

namespace Iaito {
    void initializeSettings();
    void migrateThemes();
}

#endif // COMMON_SETTINGS_UPGRADE_H
