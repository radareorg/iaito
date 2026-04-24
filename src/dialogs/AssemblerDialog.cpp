#include "dialogs/AssemblerDialog.h"

#include "common/Helpers.h"
#include "core/Iaito.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

AssemblerDialog::AssemblerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Assembler"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(680, 420);

    addressLineEdit = new QLineEdit(RAddressString(Core()->getOffset()), this);
    archComboBox = new QComboBox(this);
    cpuComboBox = new QComboBox(this);
    bitsComboBox = new QComboBox(this);
    assemblyTextEdit = new QPlainTextEdit(this);
    bytesTextEdit = new QPlainTextEdit(this);
    statusLabel = new QLabel(this);

    archComboBox->addItems(Core()->getAsmPluginNames());
    archComboBox->model()->sort(0);
    const QString currentArch = Core()->getConfig("asm.arch");
    int archIndex = archComboBox->findText(currentArch);
    if (archIndex >= 0) {
        archComboBox->setCurrentIndex(archIndex);
    }

    const QString currentBits = Core()->getConfig("asm.bits");
    bitsComboBox->addItems(
        {QStringLiteral("8"), QStringLiteral("16"), QStringLiteral("32"), QStringLiteral("64")});
    int bitsIndex = bitsComboBox->findText(currentBits);
    if (bitsIndex >= 0) {
        bitsComboBox->setCurrentIndex(bitsIndex);
    }
    reloadCpuCombo();

    assemblyTextEdit->setPlainText(QStringLiteral("nop"));
    assemblyTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    bytesTextEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    bytesTextEdit->setMinimumHeight(72);
    bytesTextEdit->setMaximumHeight(110);
    statusLabel->setMinimumHeight(statusLabel->fontMetrics().height());

    qhelpers::bindFont(addressLineEdit, false, true);
    qhelpers::bindFont(assemblyTextEdit, false, true);
    qhelpers::bindFont(bytesTextEdit, false, true);

    auto *formLayout = new QFormLayout;
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->addRow(tr("Address:"), addressLineEdit);
    formLayout->addRow(tr("Arch:"), archComboBox);
    formLayout->addRow(tr("CPU:"), cpuComboBox);
    formLayout->addRow(tr("Bits:"), bitsComboBox);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(formLayout);
    layout->addWidget(new QLabel(tr("Instructions:"), this));
    layout->addWidget(assemblyTextEdit, 1);
    layout->addWidget(new QLabel(tr("Bytes:"), this));
    layout->addWidget(bytesTextEdit);

    auto *bottomLayout = new QHBoxLayout;
    bottomLayout->addWidget(statusLabel, 1);
    bottomLayout->addWidget(buttonBox);
    layout->addLayout(bottomLayout);

    connect(addressLineEdit, &QLineEdit::textEdited, this, &AssemblerDialog::updateFromAddress);
    connect(archComboBox, &QComboBox::currentTextChanged, this, [this]() {
        reloadCpuCombo();
        updateFromSettings();
    });
    connect(cpuComboBox, &QComboBox::currentTextChanged, this, &AssemblerDialog::updateFromSettings);
    connect(bitsComboBox, &QComboBox::currentTextChanged, this, &AssemblerDialog::updateFromSettings);
    connect(
        assemblyTextEdit, &QPlainTextEdit::textChanged, this, &AssemblerDialog::updateFromAssembly);
    connect(bytesTextEdit, &QPlainTextEdit::textChanged, this, &AssemblerDialog::updateFromBytes);

    updateFromAssembly();
}

RVA AssemblerDialog::address() const
{
    const QString text = addressLineEdit->text().trimmed();
    return text.isEmpty() ? Core()->getOffset() : Core()->math(text);
}

void AssemblerDialog::setStatus(const QString &text)
{
    statusLabel->setText(text);
}

void AssemblerDialog::reloadCpuCombo()
{
    const QString currentCpu = cpuComboBox->currentText().isEmpty() ? Core()->getConfig("asm.cpu")
                                                                    : cpuComboBox->currentText();

    const bool blocked = cpuComboBox->blockSignals(true);
    cpuComboBox->clear();
    cpuComboBox->addItem(QString());

    const QString suffix = QStringLiteral("@e:asm.arch=%1").arg(archComboBox->currentText());
    const QStringList cpus = Core()
                                 ->cmd(QStringLiteral("e asm.cpu=?%1").arg(suffix))
                                 .split(QLatin1Char('\n'), IAITO_QT_SKIP_EMPTY_PARTS);
    cpuComboBox->addItems(cpus);

    int cpuIndex = cpuComboBox->findText(currentCpu);
    if (cpuIndex >= 0) {
        cpuComboBox->setCurrentIndex(cpuIndex);
    }
    cpuComboBox->blockSignals(blocked);
}

QString AssemblerDialog::temporaryEvalSuffix() const
{
    QString suffix = QStringLiteral("@e:asm.arch=%1@e:asm.bits=%2")
                         .arg(archComboBox->currentText(), bitsComboBox->currentText());
    if (!cpuComboBox->currentText().isEmpty()) {
        suffix += QStringLiteral("@e:asm.cpu=%1").arg(cpuComboBox->currentText());
    }
    return suffix;
}

QByteArray AssemblerDialog::assembleWithSettings(const QString &assembly, RVA address) const
{
    QStringList lines;
    for (const QString &line : assembly.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            lines.append(trimmed);
        }
    }

    const QString commandText = escapedCommandText(lines.join(QLatin1Char(';')));
    const QString command = QStringLiteral("\"pa %1\"%2").arg(commandText, temporaryEvalSuffix());
    const QString hex = Core()->cmdRawAt(command, address).trimmed();
    if (hex.contains(QLatin1String("ERROR:"))) {
        return {};
    }
    return IaitoCore::hexStringToBytes(hex);
}

QString AssemblerDialog::disassembleWithSettings(const QString &hex, RVA address) const
{
    const QString command = QStringLiteral("\"pad %1\"%2").arg(hex, temporaryEvalSuffix());
    return Core()->cmdRawAt(command, address).trimmed();
}

bool AssemblerDialog::normalizedHex(const QString &input, QString *hex, QString *error)
{
    QString out;
    out.reserve(input.size());

    for (int i = 0; i < input.size(); ++i) {
        const QChar c = input.at(i);
        if (c.isSpace()) {
            continue;
        }
        if (c == QLatin1Char('0') && i + 1 < input.size()
            && (input.at(i + 1) == QLatin1Char('x') || input.at(i + 1) == QLatin1Char('X'))) {
            ++i;
            continue;
        }
        if ((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
            || (c >= QLatin1Char('A') && c <= QLatin1Char('F'))) {
            out.append(c.toLower());
            continue;
        }
        if (error) {
            *error = tr("Bytes must be hexadecimal");
        }
        return false;
    }

    if (out.size() % 2 != 0) {
        if (error) {
            *error = tr("Bytes must contain whole bytes");
        }
        return false;
    }

    if (hex) {
        *hex = out;
    }
    return true;
}

QString AssemblerDialog::escapedCommandText(QString text)
{
    text.replace(QLatin1Char('"'), QStringLiteral(" # "));
    return text;
}

void AssemblerDialog::updateFromAddress()
{
    if (updating) {
        return;
    }
    if (lastInputMode == InputMode::Bytes) {
        updateFromBytes();
    } else {
        updateFromAssembly();
    }
}

void AssemblerDialog::updateFromSettings()
{
    if (updating) {
        return;
    }
    if (lastInputMode == InputMode::Bytes) {
        updateFromBytes();
    } else {
        updateFromAssembly();
    }
}

void AssemblerDialog::updateFromAssembly()
{
    if (updating) {
        return;
    }

    lastInputMode = InputMode::Assembly;
    const QString assembly = assemblyTextEdit->toPlainText();
    if (assembly.trimmed().isEmpty()) {
        updating = true;
        bytesTextEdit->clear();
        updating = false;
        setStatus(QString());
        return;
    }

    const QByteArray bytes = assembleWithSettings(assembly, address());
    if (bytes.isEmpty()) {
        setStatus(tr("Assembly error"));
        return;
    }

    updating = true;
    bytesTextEdit->setPlainText(IaitoCore::bytesToHexString(bytes));
    updating = false;
    setStatus(QString());
}

void AssemblerDialog::updateFromBytes()
{
    if (updating) {
        return;
    }

    lastInputMode = InputMode::Bytes;
    QString hex;
    QString error;
    if (!normalizedHex(bytesTextEdit->toPlainText(), &hex, &error)) {
        setStatus(error);
        return;
    }

    if (hex.isEmpty()) {
        updating = true;
        assemblyTextEdit->clear();
        updating = false;
        setStatus(QString());
        return;
    }

    const QString assembly = disassembleWithSettings(hex, address());
    if (assembly.trimmed().isEmpty() || assembly.contains(QLatin1String("invalid"))) {
        setStatus(tr("Disassembly error"));
        return;
    }

    updating = true;
    assemblyTextEdit->setPlainText(assembly.trimmed());
    updating = false;
    setStatus(QString());
}
