#ifndef ASSEMBLERDIALOG_H
#define ASSEMBLERDIALOG_H

#include "core/IaitoCommon.h"

#include <QDialog>

class QLabel;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;

class AssemblerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AssemblerDialog(QWidget *parent = nullptr);

private slots:
    void updateFromAddress();
    void updateFromAssembly();
    void updateFromBytes();
    void updateFromSettings();

private:
    enum class InputMode { Assembly, Bytes };

    RVA address() const;
    void setStatus(const QString &text);
    void reloadCpuCombo();
    QString temporaryEvalSuffix() const;
    QByteArray assembleWithSettings(const QString &assembly, RVA address) const;
    QString disassembleWithSettings(const QString &hex, RVA address) const;
    static bool normalizedHex(const QString &input, QString *hex, QString *error);
    static QString escapedCommandText(QString text);

    QLineEdit *addressLineEdit = nullptr;
    QComboBox *archComboBox = nullptr;
    QComboBox *cpuComboBox = nullptr;
    QComboBox *bitsComboBox = nullptr;
    QPlainTextEdit *assemblyTextEdit = nullptr;
    QPlainTextEdit *bytesTextEdit = nullptr;
    QLabel *statusLabel = nullptr;
    bool updating = false;
    InputMode lastInputMode = InputMode::Assembly;
};

#endif // ASSEMBLERDIALOG_H
