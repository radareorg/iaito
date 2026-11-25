#ifndef DECOMPILER_H
#define DECOMPILER_H

#include "IaitoCommon.h"
#include "R2Task.h"

#include <QObject>
#include <QString>

class QJsonObject;

/** Provides the command and output parser for decompilation in a separate radare2 session. */
class IAITO_EXPORT R2CommandDecompiler
{
public:
    virtual ~R2CommandDecompiler() = default;
    virtual QString commandAt(RVA addr) const = 0;
    virtual QString codeFromOutput(const QByteArray &output) const;

protected:
    static QString takeCodeText(RCodeMeta *code);
};

/**
 * Implements a decompiler that can be registered using
 * IaitoCore::registerDecompiler()
 */
class IAITO_EXPORT Decompiler : public QObject
{
    Q_OBJECT

private:
    const QString id;
    const QString name;

public:
    Decompiler(const QString &id, const QString &name, QObject *parent = nullptr);
    virtual ~Decompiler() = default;

    static RCodeMeta *makeWarning(QString warningMessage);

    QString getId() const { return id; }
    QString getName() const { return name; }
    virtual bool isRunning() { return false; }
    virtual bool isCancelable() { return false; }
    virtual bool prefersBackground() const { return false; }
    virtual bool requiresManualTrigger() const { return false; }
    virtual bool hasCachedResult(RVA addr) const
    {
        Q_UNUSED(addr);
        return false;
    }
    virtual QString getConfigPrefix() const { return QString(); }

    virtual QStringList listOptions();
    virtual QString getOption(const QString &key);
    virtual void setOption(const QString &key, const QString &value);

    virtual void decompileAt(RVA addr) = 0;
    virtual RCodeMeta *decompileSync(RVA addr) = 0;
    virtual void cancel() {}

signals:
    void finished(RCodeMeta *codeDecompiled);
};

class IAITO_EXPORT R2JsonDecompiler : public Decompiler, public R2CommandDecompiler
{
    Q_OBJECT

public:
    enum class AnnotationMode { OffsetsOnly, Full };

    R2JsonDecompiler(
        const QString &id,
        const QString &name,
        const QString &command,
        const QString &jsonName,
        AnnotationMode annotationMode,
        QObject *parent = nullptr);

    RCodeMeta *decompileSync(RVA addr) override;
    void decompileAt(RVA addr) override;
    bool isRunning() override { return task != nullptr; }
    QString commandAt(RVA addr) const override;
    QString codeFromOutput(const QByteArray &output) const override;

protected:
    virtual RCodeMeta *buildCodeMeta(const QJsonObject &json) const;
    QString decompileCommand(RVA addr) const;

private:
    R2Task *task = nullptr;
    const QString command;
    const QString jsonName;
    const AnnotationMode annotationMode;
};

class R2DecDecompiler : public Decompiler, public R2CommandDecompiler
{
    Q_OBJECT

private:
    R2Task *task;

public:
    explicit R2DecDecompiler(QObject *parent = nullptr);
    RCodeMeta *decompileSync(RVA addr) override;
    void decompileAt(RVA addr) override;
    QString commandAt(RVA addr) const override;
    QString codeFromOutput(const QByteArray &output) const override;

    bool isRunning() override { return task != nullptr; }
    QString getConfigPrefix() const override { return QStringLiteral("r2dec"); }

    static bool isAvailable();
};

#endif // DECOMPILER_H
