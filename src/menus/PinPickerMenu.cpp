#include "PinPickerMenu.h"

#include <QAction>
#include <QInputDialog>
#include <QLineEdit>

PinPickerMenu::PinPickerMenu(const QString &title, QWidget *parent)
    : QMenu(title, parent)
{
    addPreset(QStringLiteral("📌"), QStringLiteral("pin"));
    addPreset(QStringLiteral("🔥"), QStringLiteral("fire"));
    addPreset(QStringLiteral("❤"), QStringLiteral("heart"));
    addPreset(QStringLiteral("👍"), QStringLiteral("thumbsup"));
    addPreset(QStringLiteral("👎"), QStringLiteral("thumbsdown"));
    addPreset(QStringLiteral("✅"), QStringLiteral("check"));
    addPreset(QStringLiteral("❌"), QStringLiteral("cross"));
    addPreset(QStringLiteral("❓"), QStringLiteral("question"));
    addPreset(QStringLiteral("🐛"), QStringLiteral("bug"));
    addPreset(QStringLiteral("🚀"), QStringLiteral("rocket"));
    addPreset(QStringLiteral("💣"), QStringLiteral("bomb"));
    addPreset(QStringLiteral("💀"), QStringLiteral("skull"));
    addPreset(QStringLiteral("👀"), QStringLiteral("eyes"));
    addPreset(QStringLiteral("🤔"), QStringLiteral("thinking"));
    addPreset(QStringLiteral("😢"), QStringLiteral("cry"));
    addPreset(QStringLiteral("🔒"), QStringLiteral("lock"));
    addPreset(QStringLiteral("🔑"), QStringLiteral("key"));
    addSeparator();
    addCustom();
    addReset();
}

void PinPickerMenu::addPreset(const QString &emoji, const QString &name)
{
    QAction *a = addAction(QStringLiteral("%1  %2").arg(emoji, name));
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
