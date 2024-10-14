#ifndef COMMON_SETTINGS_UPGRADE_H
#define COMMON_SETTINGS_UPGRADE_H

#include <core/Iaito.h>
#include <QSettings>

namespace Iaito {
void initializeSettings();
void migrateThemes();
} // namespace Iaito

#endif // COMMON_SETTINGS_UPGRADE_H
