#ifndef ASSEMBLERDIALOG_H
#define ASSEMBLERDIALOG_H

#include "core/IaitoCommon.h"

#include <QDialog>
#include <QList>
#include <QString>

class QLabel;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

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
    void applyInstructionFilter();
    void insertInstructionMnemonic(QTreeWidgetItem *item, int column);

private:
    enum class InputMode { Assembly, Bytes };

    struct InstructionDescription
    {
        QString mnemonic;
        QString description;
    };

    RVA address() const;
    void setStatus(const QString &text);
    void reloadCpuCombo();
    void reloadInstructionDescriptions();
    QString temporaryEvalSuffix() const;
    QByteArray assembleWithSettings(const QString &assembly, RVA address) const;
    QString disassembleWithSettings(const QString &hex, RVA address) const;
    QList<InstructionDescription> instructionDescriptions() const;
    static bool normalizedHex(const QString &input, QString *hex, QString *error);
    static QString escapedCommandText(QString text);

    QLineEdit *addressLineEdit = nullptr;
    QComboBox *archComboBox = nullptr;
    QComboBox *cpuComboBox = nullptr;
    QComboBox *bitsComboBox = nullptr;
    QPlainTextEdit *assemblyTextEdit = nullptr;
    QPlainTextEdit *bytesTextEdit = nullptr;
    QPushButton *instructionToggleButton = nullptr;
    QLineEdit *instructionFilterLineEdit = nullptr;
    QTreeWidget *instructionTreeWidget = nullptr;
    QWidget *instructionPanel = nullptr;
    QLabel *statusLabel = nullptr;
    bool updating = false;
    InputMode lastInputMode = InputMode::Assembly;
};

#endif // ASSEMBLERDIALOG_H
