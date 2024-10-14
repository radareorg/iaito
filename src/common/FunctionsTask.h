
#ifndef FUNCTIONSTASK_H
#define FUNCTIONSTASK_H

#include "common/AsyncTask.h"
#include "core/Iaito.h"

class FunctionsTask : public AsyncTask
{
    Q_OBJECT

public:
    QString getTitle() override { return tr("Fetching Functions"); }

signals:
    void fetchFinished(const QList<FunctionDescription> &strings);

protected:
    void runTask() override
    {
#if MONOTHREAD
        QList<FunctionDescription> functions;
#else
        auto functions = Core()->getAllFunctions();
#endif
        emit fetchFinished(functions);
    }
};

#endif // FUNCTIONSTASK_H
