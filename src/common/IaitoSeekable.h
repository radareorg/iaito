#pragma once

#include "core/Iaito.h"
#include <QVector>

class MainWindow;

class IAITO_EXPORT IaitoSeekable : public QObject
{
    Q_OBJECT

public:
    explicit IaitoSeekable(QObject *parent = nullptr);
    ~IaitoSeekable();

    /**
     * @brief seek changes current offset.
     * If the seekable is synchronized with Core, then
     * the Core offset will be modified and then the IaitoCore::seekChanged
     * signal will be emitted.
     * In any case, IaitoSeekable::seekableSeekChanged is emitted.
     * @param addr the location to seek at.
     */
    void seek(RVA addr);

    /**
     * @brief setSynchronization sets
     * Core seek synchronization.
     */
    void setSynchronization(bool sync);

    /**
     * @brief getOffset returns the seekable offset.
     * If the seekable is synchronized with Core, this function
     * is similar to Core()->getOffset.
     * If it's not synchronized, it will return the seekable current seek.
     * @return the seekable current offset.
     */
    RVA getOffset();

    /**
     * @brief isSynchronized tells whether the seekable
     * is synchronized with Core or not.
     * @return boolean
     */
    bool isSynchronized();

    bool canSeekPrev() const;
    bool canSeekNext() const;

    /**
     * @brief seekToReference will seek to the function or the object which is
     * referenced in a given offset
     * @param offset - an address that contains a reference to jump to
     */
    void seekToReference(RVA offset);

public slots:
    /**
     * @brief seekPrev seeks to last location.
     */
    void seekPrev();
    void seekNext();

    /**
     * @brief toggleSyncWithCore toggles
     * Core seek synchronization.
     */
    void toggleSynchronization();

private slots:
    /**
     * @brief onCoreSeekChanged
     */
    void onCoreSeekChanged(RVA addr);

private:
    /**
     * @brief widgetOffset widget seek location.
     */
    RVA widgetOffset = RVA_INVALID;

    QVector<RVA> seekHistory;
    int historyIndex = 0;

    /**
     * @brief synchronized tells with the seekable's offset is
     * synchronized with core or not.
     */
    bool synchronized = true;

    /**
     * @brief internal method for changing the seek
     */
    void updateSeek(RVA addr);

signals:
    void seekableSeekChanged(RVA addr);
    void syncChanged();
    void historyChanged();
};
