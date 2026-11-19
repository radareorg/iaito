#include "FileDialog.h"
#include "FilePaths.h"
#include "core/Iaito.h"

#include <QApplication>
#include <QFileDialog>
#include <QInputMethod>
#include <QStandardPaths>

QString Iaito::getOpenFileName(
    QWidget *parent, const QString &caption, const QUrl &directory, const QString &filter)
{
    const bool mobile = QGuiApplication::platformName() == QLatin1String("ios")
                        || QGuiApplication::platformName() == QLatin1String("android")
                        || qEnvironmentVariableIntValue("IAITO_MOBILE_UI") == 1;
    if (mobile) {
        // A touch on Browse must also leave any manually edited path behind.
        // Otherwise restoring focus after cancellation reopens the keyboard.
        if (auto *focus = QApplication::focusWidget()) {
            focus->clearFocus();
        }
        qApp->inputMethod()->hide();
    }
    const QUrl selected
        = QFileDialog::getOpenFileUrl(parent, caption, directory, filter, nullptr, QFILEDIALOG_FLAGS);
    if (selected.isEmpty()) {
        return {};
    }
    const QString name = fileNameFromUrl(selected);
#ifdef Q_OS_IOS
    // Qt requests an import from UIDocumentPickerViewController. Its returned
    // copy is temporary; radare2 needs a normal, persistent sandbox path.
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString error;
    const QString imported = importFile(
        name, documents.isEmpty() ? QString() : QDir(documents).filePath("Imports"), &error);
    if (imported.isEmpty()) {
        QMessageBox::warning(
            parent, caption, QObject::tr("Cannot import the selected file: %1").arg(error));
    }
    return imported;
#else
    return name;
#endif
}
