#ifndef IAITO_DEEPLINK_H
#define IAITO_DEEPLINK_H

#include <QString>

class MainWindow;

// iaito:// deep link handling. See DEEPLINK.md for the URI specification.
namespace DeepLink {

// Returns true if arg looks like an iaito:// deep link.
bool isDeepLink(const QString &arg);

// Parses uri and opens the referenced file/project/sha256/bytes, applying the
// addr/view/size query parameters once the UI is ready. Returns false when the
// uri is not a valid iaito:// deep link.
bool handle(MainWindow *main, const QString &uri);

} // namespace DeepLink

#endif // IAITO_DEEPLINK_H
