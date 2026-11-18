#ifndef IAITO_ANDROID_H
#define IAITO_ANDROID_H

#include <QString>

#ifdef Q_OS_ANDROID
struct r_io_t;
#endif

namespace Iaito {
// Install APK assets in private storage before initializing radare2.
bool initializeAndroid(QString *error);
#ifdef Q_OS_ANDROID
// Install the app's ContentResolver in the singleton core's IO plugin list.
bool initializeAndroidContent(r_io_t *io);
#endif
} // namespace Iaito

#endif // IAITO_ANDROID_H
