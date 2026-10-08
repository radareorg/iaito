#include "FridaBackend.h"

#include "common/R2Task.h"
#include "core/Iaito.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

FridaBackend::FridaBackend(QObject *parent)
    : QObject(parent)
{}

bool FridaBackend::pluginAvailable()
{
    if (availabilityChecked_) {
        return available_;
    }
    availabilityChecked_ = true;
    const QString listed = Core()->cmd(QStringLiteral("oL"));
    available_ = listed.contains(QStringLiteral("frida"), Qt::CaseInsensitive);
    return available_;
}

int FridaBackend::currentFd() const
{
    bool ok = false;
    const int fd = Core()->cmd(QStringLiteral("oqq")).trimmed().toInt(&ok);
    return ok ? fd : -1;
}

QSet<int> FridaBackend::openFds() const
{
    QSet<int> fds;
    const QJsonArray files = Core()->cmdj(QStringLiteral("oj")).array();
    for (const QJsonValue value : files) {
        fds.insert(value.toObject().value(QStringLiteral("fd")).toInt());
    }
    return fds;
}

int FridaBackend::findNewFridaFd(const QSet<int> &before) const
{
    const QJsonArray files = Core()->cmdj(QStringLiteral("oj")).array();
    for (const QJsonValue value : files) {
        const QJsonObject file = value.toObject();
        const int fd = file.value(QStringLiteral("fd")).toInt();
        const QString uri = file.value(QStringLiteral("uri")).toString();
        if (!before.contains(fd) && uri.startsWith(QStringLiteral("frida://"))) {
            return fd;
        }
    }
    return -1;
}

void FridaBackend::closeFd(int fd)
{
    if (fd < 0) {
        return;
    }
    Core()->cmd(QStringLiteral("o-") + QString::number(fd));
    if (fridaFd_ == fd) {
        fridaFd_ = -1;
    }
}

int FridaBackend::restoreFd() const
{
    const int current = currentFd();
    if (current > 0 && current != fridaFd_) {
        return current;
    }
    if (hostFd_ > 0 && hostFd_ != fridaFd_) {
        return hostFd_;
    }
    return -1;
}

void FridaBackend::runBatch(const QString &batch, const std::function<void(QString)> &done)
{
    if (busy_) {
        if (done) {
            done(QString());
        }
        return;
    }
    const QString savedColor = Core()->getConfig(QStringLiteral("scr.color"));
    QString script = QStringLiteral("e scr.color=0\n");
    script += batch;
    if (!script.endsWith(QLatin1Char('\n'))) {
        script += QLatin1Char('\n');
    }
    script += QStringLiteral("e scr.color=");
    script += savedColor;
    script += QLatin1Char('\n');

    busy_ = true;
    task_ = QSharedPointer<R2Task>(new R2Task(script));
    connect(task_.data(), &R2Task::finished, this, [this, done, savedColor]() {
        const QString output = task_ ? task_->getResult() : QString();
        task_.clear();
        busy_ = false;
        Core()->setConfig(QStringLiteral("scr.color"), savedColor);
        if (done) {
            done(output);
        }
    });
    task_->startTask();
}

QString FridaBackend::fridaCommand(const QString &command)
{
    if (fridaFd_ < 0 || command.isEmpty()) {
        return QString();
    }
    const int back = restoreFd();
    QString batch = QStringLiteral("o=") + QString::number(fridaFd_) + QStringLiteral("\n:")
                    + command + QLatin1Char('\n');
    if (back > 0) {
        batch += QStringLiteral("o=") + QString::number(back) + QLatin1Char('\n');
    }
    return Core()->cmd(batch);
}

static QString stripAnsi(QString text)
{
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;]*m"));
    text.replace(ansi, QString());
    text.replace(QChar(0x2502), QLatin1Char('|'));
    return text;
}

static bool isSeparatorLine(const QString &line)
{
    if (line.isEmpty()) {
        return true;
    }
    for (const QChar ch : line) {
        if (ch.isSpace() || ch == QLatin1Char('-') || ch == QLatin1Char('|')
            || ch == QLatin1Char('+') || ch == QChar(0x2500) || ch == QChar(0x250c)
            || ch == QChar(0x2510)
            || ch == QChar(0x2514) || ch == QChar(0x2518) || ch == QChar(0x251c)
            || ch == QChar(0x2524) || ch == QChar(0x252c) || ch == QChar(0x2534)
            || ch == QChar(0x253c)) {
            continue;
        }
        return false;
    }
    return true;
}

static QStringList cellsOf(const QString &line)
{
    QString trimmed = line.trimmed();
    if (trimmed.startsWith(QLatin1Char('|'))) {
        QStringList cells;
        const QStringList parts = trimmed.split(QLatin1Char('|'));
        for (QString part : parts) {
            part = part.trimmed();
            if (!part.isEmpty()) {
                cells.append(part);
            }
        }
        return cells;
    }
    return trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

QList<FridaDeviceInfo> FridaBackend::parseDevices(const QString &output)
{
    QList<FridaDeviceInfo> devices;
    const QStringList lines = stripAnsi(output).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        if (isSeparatorLine(raw.trimmed())) {
            continue;
        }
        const QStringList cells = cellsOf(raw);
        if (cells.size() < 3) {
            continue;
        }
        const QString type = cells.at(1).toLower();
        if (type != QStringLiteral("local") && type != QStringLiteral("usb")
            && type != QStringLiteral("remote")) {
            continue;
        }
        FridaDeviceInfo device;
        device.id = cells.at(0);
        device.type = type;
        device.name = cells.mid(2).join(QLatin1Char(' '));
        devices.append(device);
    }
    return devices;
}

QList<FridaTargetInfo> FridaBackend::parseTargets(const QString &output, FridaListKind kind)
{
    QList<FridaTargetInfo> targets;
    const QStringList lines = stripAnsi(output).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        if (isSeparatorLine(raw.trimmed())) {
            continue;
        }
        const QStringList cells = cellsOf(raw);
        if (cells.isEmpty()) {
            continue;
        }
        const QString pid = cells.at(0);
        const bool pidOk = pid == QStringLiteral("-")
                           || QRegularExpression(QStringLiteral("^\\d+$")).match(pid).hasMatch();
        if (!pidOk || pid.compare(QStringLiteral("PID"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        FridaTargetInfo target;
        target.pid = pid;
        if (kind == FridaListKind::Applications && cells.size() >= 3) {
            target.identifier = cells.last();
            target.name = cells.mid(1, cells.size() - 2).join(QLatin1Char(' '));
        } else if (cells.size() >= 2) {
            target.name = cells.mid(1).join(QLatin1Char(' '));
        }
        targets.append(target);
    }
    return targets;
}

QJsonDocument FridaBackend::parseJsonLoose(const QString &text)
{
    const int arrayAt = text.indexOf(QLatin1Char('['));
    const int objectAt = text.indexOf(QLatin1Char('{'));
    int start = -1;
    if (arrayAt >= 0 && (objectAt < 0 || arrayAt < objectAt)) {
        start = arrayAt;
    } else {
        start = objectAt;
    }
    if (start < 0) {
        return QJsonDocument();
    }
    const QChar open = text.at(start);
    const QChar close = open == QLatin1Char('[') ? QLatin1Char(']') : QLatin1Char('}');
    const int end = text.lastIndexOf(close);
    if (end < start) {
        return QJsonDocument();
    }
    return QJsonDocument::fromJson(text.mid(start, end - start + 1).toUtf8());
}

quint64 FridaBackend::parseAddress(const QJsonValue &value)
{
    if (value.isString()) {
        QString text = value.toString().trimmed();
        bool ok = false;
        int base = 10;
        if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            text = text.mid(2);
            base = 16;
        }
        const quint64 parsed = text.toULongLong(&ok, base);
        return ok ? parsed : 0;
    }
    if (value.isDouble()) {
        return static_cast<quint64>(value.toDouble());
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (const char *key : {"address", "value", "$handle", "base"}) {
            if (object.contains(QLatin1String(key))) {
                return parseAddress(object.value(QLatin1String(key)));
            }
        }
    }
    return 0;
}
