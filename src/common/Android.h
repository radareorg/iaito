#ifndef IAITO_ANDROID_H
#define IAITO_ANDROID_H

#include <QString>

namespace Iaito {
// Install APK assets in private storage before initializing radare2.
bool initializeAndroid(QString *error);
} // namespace Iaito

#endif // IAITO_ANDROID_H
