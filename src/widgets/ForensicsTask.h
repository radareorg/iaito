#pragma once

#include "common/AsyncTask.h"

#include <atomic>
#include <functional>
#include <utility>

class ForensicsTask : public AsyncTask
{
    Q_OBJECT

public:
    using Action = std::function<void(ForensicsTask &)>;

    ForensicsTask(QString title, Action action)
        : title(std::move(title))
        , action(std::move(action))
    {}

    QString getTitle() override { return title; }
    void interrupt() override { stopped = true; }
    bool cancelled() const { return stopped.load(); }

    void report(const QString &text, quint64 done = 0, quint64 total = 0)
    {
        if (!lastReport.isValid() || lastReport.elapsed() >= 50 || text != lastText) {
            lastText = text;
            lastReport.start();
            emit progressChanged(text, done, total);
        }
    }

signals:
    void progressChanged(const QString &text, quint64 done, quint64 total);

protected:
    void runTask() override { action(*this); }

private:
    QString title;
    Action action;
    std::atomic_bool stopped{false};
    QElapsedTimer lastReport;
    QString lastText;
};
