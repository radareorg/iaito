#ifndef SHORTCUTMANAGER_H
#define SHORTCUTMANAGER_H

#include "core/IaitoCommon.h"
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QVector>

#define ShortcutMgr() (ShortcutManager::instance())

class QShortcut;
class QAction;
class QWidget;
class QKeyEvent;

enum class ShortcutScope {
    Global,
    Disassembly,
    Decompiler,
    Hexdump,
    Graph,
    GraphOverview,
    Console,
    AddressableList,
    Debug,
    Overview,
};

struct ShortcutConflict
{
    QString idA;
    QString idB;
    QKeySequence sequence;
};

struct CommandShortcut
{
    QKeySequence key;
    QString command;
    bool needsInput = false;
};

class IAITO_EXPORT ShortcutManager : public QObject
{
    Q_OBJECT
public:
    static ShortcutManager *instance();

    void load();

    QStringList ids() const;
    static QList<ShortcutScope> scopes();
    QStringList idsForScope(ShortcutScope scope) const;
    static QString scopeName(ShortcutScope scope);
    QString displayName(const QString &id) const;
    ShortcutScope scope(const QString &id) const;
    bool contains(const QString &id) const;

    QList<QKeySequence> sequences(const QString &id) const;
    QKeySequence sequence(const QString &id) const;
    QList<QKeySequence> defaults(const QString &id) const;
    bool isOverridden(const QString &id) const;

    void setSequences(const QString &id, const QList<QKeySequence> &seqs);
    void setSequence(const QString &id, const QKeySequence &seq);
    void resetToDefault(const QString &id);
    void resetAll();

    QShortcut *registerShortcut(
        const QString &id, QWidget *parent, Qt::ShortcutContext context = Qt::WindowShortcut);
    void bindAction(const QString &id, QAction *action);

    bool matches(const QString &id, const QKeyEvent *event) const;

    QList<ShortcutConflict> conflicts() const;

    QVector<CommandShortcut> commandShortcuts() const;
    void setCommandShortcuts(const QVector<CommandShortcut> &list);
    int matchCommandShortcut(const QKeyEvent *event) const;
    void migrateLegacyCommandKeys();

signals:
    void shortcutChanged(const QString &id);
    void shortcutsReset();
    void commandShortcutsChanged();

private:
    explicit ShortcutManager(QObject *parent = nullptr);
    void applyTo(const QString &id);
    void persist(const QString &id);

    struct Binding
    {
        QPointer<QShortcut> shortcut;
        QPointer<QAction> action;
        int seqIndex = 0;
    };

    QSettings s;
    QHash<QString, QList<QKeySequence>> m_overrides;
    QHash<QString, QVector<Binding>> m_bindings;
    QVector<CommandShortcut> m_commandShortcuts;
};

#endif // SHORTCUTMANAGER_H
