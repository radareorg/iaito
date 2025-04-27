// ShortcutKeys.h
#ifndef SHORTCUTKEYS_H
#define SHORTCUTKEYS_H

#include <QObject>
#include <QMap>
#include <QChar>
#include "core/Iaito.h"

/**
 * @brief Singleton manager for storing and retrieving shortcut marks.
 */
class ShortcutKeys : public QObject {
    Q_OBJECT
public:
    /**
     * @brief instance returns the singleton instance.
     */
    static ShortcutKeys* instance();

    /**
     * @brief setMark associates a key with an address.
     * @param key the key (e.g. 'a')
     * @param addr the address to mark
     * @return true if set, false otherwise
     */
    bool setMark(const QChar& key, RVA addr);

    /**
     * @brief removeMark removes an existing key mapping.
     * @param key the key to remove
     * @return true if removed, false if not found
     */
    bool removeMark(const QChar& key);

    /**
     * @brief hasMark returns whether a mapping exists.
     */
    bool hasMark(const QChar& key) const;

    /**
     * @brief getMark retrieves the address for a given key.
     * @return address or RVA_INVALID if not found
     */
    RVA getMark(const QChar& key) const;

    /**
     * @brief marks returns all saved mappings.
     */
    QMap<QChar, RVA> marks() const;

signals:
    /**
     * @brief marksChanged emitted when mappings are updated.
     */
    void marksChanged();

private:
    explicit ShortcutKeys(QObject* parent = nullptr);
    QMap<QChar, RVA> m_marks;
};

#endif // SHORTCUTKEYS_H