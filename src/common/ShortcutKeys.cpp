// ShortcutKeys.cpp
#include "common/ShortcutKeys.h"

ShortcutKeys *ShortcutKeys::instance()
{
    static ShortcutKeys *s_instance = new ShortcutKeys();
    return s_instance;
}

ShortcutKeys::ShortcutKeys(QObject *parent)
    : QObject(parent)
{}

bool ShortcutKeys::setMark(const QChar &key, RVA addr)
{
    m_marks[key] = addr;
    emit marksChanged();
    return true;
}

bool ShortcutKeys::removeMark(const QChar &key)
{
    if (!m_marks.contains(key)) {
        return false;
    }
    m_marks.remove(key);
    emit marksChanged();
    return true;
}

bool ShortcutKeys::hasMark(const QChar &key) const
{
    return m_marks.contains(key);
}

RVA ShortcutKeys::getMark(const QChar &key) const
{
    return m_marks.value(key, RVA_INVALID);
}

QMap<QChar, RVA> ShortcutKeys::marks() const
{
    return m_marks;
}