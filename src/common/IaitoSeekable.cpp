#include "IaitoSeekable.h"
#include "core/MainWindow.h"

#include <QPlainTextEdit>

IaitoSeekable::IaitoSeekable(QObject *parent)
    : QObject(parent)
{
    connect(Core(), &IaitoCore::seekChanged, this, &IaitoSeekable::onCoreSeekChanged);
}

IaitoSeekable::~IaitoSeekable() {}

void IaitoSeekable::setSynchronization(bool sync)
{
    synchronized = sync;
    onCoreSeekChanged(Core()->getOffset());
    emit syncChanged();
}

void IaitoSeekable::onCoreSeekChanged(RVA addr)
{
    if (synchronized && widgetOffset != addr) {
        updateSeek(addr, true);
    }
}

void IaitoSeekable::updateSeek(RVA addr, bool localOnly)
{
    previousOffset = widgetOffset;
    widgetOffset = addr;
    if (synchronized && !localOnly) {
        Core()->seek(addr);
    }

    emit seekableSeekChanged(addr);
}

void IaitoSeekable::seekPrev()
{
    if (synchronized) {
        Core()->seekPrev();
    } else {
        this->seek(previousOffset);
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
