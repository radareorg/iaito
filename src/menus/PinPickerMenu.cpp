#include "PinPickerMenu.h"

#include <QAction>
#include <QInputDialog>
#include <QLineEdit>

PinPickerMenu::PinPickerMenu(const QString &title, QWidget *parent)
    : QMenu(title, parent)
{
    addPreset(QStringLiteral("📍"));
    addPreset(QStringLiteral("🔥"));
    addPreset(QStringLiteral("❤️"));
    addPreset(QStringLiteral("😱"));
    addPreset(QStringLiteral("✅"));
    addPreset(QStringLiteral("😢"));
    addSeparator();
    addCustom();
    addReset();
}

void PinPickerMenu::addPreset(const QString &emoji)
{
    QAction *a = addAction(emoji);
    connect(a, &QAction::triggered, this, [this, emoji]() { emit pinPicked(emoji); });
}

void PinPickerMenu::addCustom()
{
    QAction *a = addAction(tr("Custom..."));
    connect(a, &QAction::triggered, this, [this]() {
        bool ok = false;
#if defined(Q_OS_MAC)
        const QString hint = tr("Emoji or text (press Cmd+Ctrl+Space to open the emoji picker):");
#else
        const QString hint = tr("Emoji or text:");
#endif
        QString text = QInputDialog::getText(
            this, tr("Pin function"), hint, QLineEdit::Normal, QString(), &ok);
        if (ok && !text.isEmpty()) {
            emit pinPicked(text);
        }
    });
}

void PinPickerMenu::addReset()
{
    QAction *a = addAction(tr("Unpin"));
    connect(a, &QAction::triggered, this, [this]() { emit pinPicked(QString()); });
}
