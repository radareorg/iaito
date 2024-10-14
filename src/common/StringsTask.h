
#ifndef STRINGSASYNCTASK_H
#define STRINGSASYNCTASK_H

#include "common/AsyncTask.h"
#include "core/Iaito.h"

class StringsTask : public AsyncTask
{
    Q_OBJECT

public:
    QString getTitle() override { return tr("Searching for Strings"); }

signals:
    void stringSearchFinished(const QList<StringDescription> &strings);

protected:
    void runTask() override
    {
#if MONOTHREAD
        QList<StringDescription> strings;
#else
        auto strings = Core()->getAllStrings();
#endif
        emit stringSearchFinished(strings);
    }
};

#endif // STRINGSASYNCTASK_H
