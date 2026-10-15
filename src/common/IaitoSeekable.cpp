#include "IaitoSeekable.h"

IaitoSeekable::IaitoSeekable(QObject *parent)
    : QObject(parent)
{
    widgetOffset = Core()->getOffset();
    connect(Core(), &IaitoCore::seekChanged, this, &IaitoSeekable::onCoreSeekChanged);
}

IaitoSeekable::~IaitoSeekable() {}

void IaitoSeekable::setSynchronization(bool sync)
{
    if (synchronized == sync) {
        return;
    }
    const RVA address = Core()->getOffset();
    synchronized = sync;
    seekHistory = {address};
    historyIndex = 0;
    updateSeek(address);
    emit syncChanged();
    emit historyChanged();
}

void IaitoSeekable::onCoreSeekChanged(RVA addr)
{
    if (synchronized) {
        updateSeek(addr);
        emit historyChanged();
    }
}

void IaitoSeekable::seek(RVA addr)
{
    if (addr == RVA_INVALID) {
        return;
    }
    if (synchronized) {
        Core()->seek(addr);
        return;
    }
    if (addr == widgetOffset) {
        return;
    }
    seekHistory.resize(historyIndex + 1);
    seekHistory.append(addr);
    ++historyIndex;
    updateSeek(addr);
    emit historyChanged();
}

void IaitoSeekable::updateSeek(RVA addr)
{
    if (addr == RVA_INVALID || addr == widgetOffset) {
        return;
    }
    widgetOffset = addr;
    emit seekableSeekChanged(addr);
}

bool IaitoSeekable::canSeekPrev() const
{
    if (synchronized) {
        return Core()->core()->io->undo.undos > 0;
    }
    return historyIndex > 0;
}

bool IaitoSeekable::canSeekNext() const
{
    if (synchronized) {
        return Core()->core()->io->undo.redos > 0;
    }
    return historyIndex + 1 < seekHistory.size();
}

void IaitoSeekable::seekPrev()
{
    if (synchronized) {
        Core()->seekPrev();
    } else if (canSeekPrev()) {
        updateSeek(seekHistory.at(--historyIndex));
        emit historyChanged();
    }
}

void IaitoSeekable::seekNext()
{
    if (synchronized) {
        Core()->seekNext();
    } else if (canSeekNext()) {
        updateSeek(seekHistory.at(++historyIndex));
        emit historyChanged();
    }
}

RVA IaitoSeekable::getOffset()
{
    return (synchronized) ? Core()->getOffset() : widgetOffset;
}

void IaitoSeekable::toggleSynchronization()
{
    setSynchronization(!synchronized);
}

bool IaitoSeekable::isSynchronized()
{
    return synchronized;
}

void IaitoSeekable::seekToReference(RVA offset)
{
    if (offset == RVA_INVALID) {
        return;
    }

    RVA target;
    QList<XrefDescription> refs = Core()->getXRefs(offset, false, false);

    if (refs.length()) {
        if (refs.length() > 1) {
            qWarning() << tr("More than one (%1) references here. Weird "
                             "behaviour expected.")
                              .arg(refs.length());
        }

        target = refs.at(0).to;
        if (target != RVA_INVALID) {
            seek(target);
        }
    }
}
