
#include "R2DecaiDecompiler.h"
#include "Iaito.h"

#include <QJsonArray>
#include <QJsonObject>

R2DecaiDecompiler::R2DecaiDecompiler(QObject *parent)
    : Decompiler("decai", "decai", parent)
{
    task = nullptr;
}

bool R2DecaiDecompiler::isAvailable()
{
    return Core()->cmdList("e cmd.pdc=?").contains(QStringLiteral("decai"));
}

RCodeMeta *R2DecaiDecompiler::decompileSync(RVA addr)
{
    auto document = Core()->cmd("decai -d @ " + QString::number(addr));
    RCodeMeta *code = r_codemeta_new("");
    // TODO: decai have no json output or source-line information
    code->code = strdup(document.toStdString().c_str());
    return code;
}

static QStringList parseDecaiEvalLines(const QString &output)
{
    static const QLatin1String kPrefix("decai -e ");
    QStringList result;
    for (const QString &raw : output.split(QLatin1Char('\n'))) {
        QString line = raw;
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if (!line.startsWith(kPrefix)) {
            continue;
        }
        QString kv = line.mid(kPrefix.size());
        if (!kv.contains(QLatin1Char('='))) {
            continue;
        }
        result.append(kv);
    }
    return result;
}

QStringList R2DecaiDecompiler::listOptions()
{
    QString output = Core()->cmd("decai -e");
    return parseDecaiEvalLines(output);
}

QString R2DecaiDecompiler::getOption(const QString &key)
{
    if (key.isEmpty()) {
        return QString();
    }
    for (const QString &kv : listOptions()) {
        int eq = kv.indexOf(QLatin1Char('='));
        if (eq < 0) {
            continue;
        }
        if (kv.left(eq) == key) {
            return kv.mid(eq + 1);
        }
    }
    return QString();
}

void R2DecaiDecompiler::setOption(const QString &key, const QString &value)
{
    if (key.isEmpty()) {
        return;
    }
    Core()->cmd(QStringLiteral("decai -e %1=%2").arg(key, value));
}

void R2DecaiDecompiler::decompileAt(RVA addr)
{
    if (task) {
        return;
    }
    task = new R2Task("decai -d @ " + QString::number(addr));
    connect(task, &R2Task::finished, this, [this]() {
        QString text = task->getResult();
        delete task;
        task = nullptr;
        RCodeMeta *code = r_codemeta_new("");
        code->code = strdup(text.toStdString().c_str());
        emit finished(code);
    });
    task->startTask();
}
