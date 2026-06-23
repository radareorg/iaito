#ifndef IAITO_DEEPLINK_H
#define IAITO_DEEPLINK_H

#include "core/IaitoCommon.h"

#include <QString>

class MainWindow;
class QWidget;

// iaito:// deep link handling. See DEEPLINK.md for the URI specification.
namespace DeepLink {

// Returns true if arg looks like an iaito:// deep link.
bool isDeepLink(const QString &arg);

// Parses uri and opens the referenced file/project/sha256/bytes, applying the
// addr/view/size query parameters once the UI is ready. Returns false when the
// uri is not a valid iaito:// deep link.
bool handle(MainWindow *main, const QString &uri);

// Builds an iaito:// deep link pointing at offset (and optionally a view) in the
// currently open file. Prompts the user to choose how the file is referenced
// (loaded project, sha256 or absolute path). Returns an empty string when the
// user cancels or no reference is available.
QString buildForOffset(QWidget *parent, RVA offset, const QString &view = QString());

// buildForOffset + copy the result to the clipboard. No-op when cancelled.
void copyForOffset(QWidget *parent, RVA offset, const QString &view = QString());

// If posInLine falls inside an iaito:// link contained in line, returns that
// link, otherwise an empty string. Used to make links in comments clickable.
QString linkAt(const QString &line, int posInLine);

} // namespace DeepLink

#endif // IAITO_DEEPLINK_H
