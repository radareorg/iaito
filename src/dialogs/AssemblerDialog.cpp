#include "dialogs/AssemblerDialog.h"

#include "common/Helpers.h"
#include "core/Iaito.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextCursor>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

AssemblerDialog::AssemblerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Assembler"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(980, 520);

    addressLineEdit = new QLineEdit(RAddressString(Core()->getOffset()), this);
    archComboBox = new QComboBox(this);
    cpuComboBox = new QComboBox(this);
    bitsComboBox = new QComboBox(this);
    assemblyTextEdit = new QPlainTextEdit(this);
    bytesTextEdit = new QPlainTextEdit(this);
    instructionToggleButton = new QPushButton(tr("Instructions"), this);
    instructionFilterLineEdit = new QLineEdit(this);
    instructionTreeWidget = new QTreeWidget(this);
    instructionPanel = new QWidget(this);
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
    instructionToggleButton->setCheckable(true);
    instructionToggleButton->setChecked(true);
    instructionFilterLineEdit->setPlaceholderText(tr("Filter instructions"));
    instructionTreeWidget->setColumnCount(2);
    instructionTreeWidget->setHeaderLabels({tr("Instruction"), tr("Description")});
    instructionTreeWidget->setAlternatingRowColors(true);
    instructionTreeWidget->setRootIsDecorated(false);
    instructionTreeWidget->setSortingEnabled(true);
    instructionTreeWidget->sortByColumn(0, Qt::AscendingOrder);
    instructionTreeWidget->header()->setStretchLastSection(true);
    instructionTreeWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    statusLabel->setMinimumHeight(statusLabel->fontMetrics().height());

    qhelpers::bindFont(addressLineEdit, false, true);
    qhelpers::bindFont(assemblyTextEdit, false, true);
    qhelpers::bindFont(bytesTextEdit, false, true);
    qhelpers::bindFont(instructionFilterLineEdit, false, true);
    qhelpers::bindFont(instructionTreeWidget, false, true);

    auto *formLayout = new QFormLayout;
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->addRow(tr("Address:"), addressLineEdit);
    formLayout->addRow(tr("Arch:"), archComboBox);
    formLayout->addRow(tr("CPU:"), cpuComboBox);
    formLayout->addRow(tr("Bits:"), bitsComboBox);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *leftWidget = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addLayout(formLayout);
    leftLayout->addWidget(new QLabel(tr("Instructions:"), this));
    leftLayout->addWidget(assemblyTextEdit, 1);
    leftLayout->addWidget(new QLabel(tr("Bytes:"), this));
    leftLayout->addWidget(bytesTextEdit);

    auto *instructionLayout = new QVBoxLayout(instructionPanel);
    instructionLayout->setContentsMargins(0, 0, 0, 0);
    instructionLayout->addWidget(instructionFilterLineEdit);
    instructionLayout->addWidget(instructionTreeWidget, 1);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(leftWidget);
    splitter->addWidget(instructionPanel);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({620, 320});

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);

    auto *bottomLayout = new QHBoxLayout;
    bottomLayout->addWidget(statusLabel, 1);
    bottomLayout->addWidget(instructionToggleButton);
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
    connect(instructionToggleButton, &QPushButton::toggled, instructionPanel, &QWidget::setVisible);
    connect(
        instructionFilterLineEdit,
        &QLineEdit::textChanged,
        this,
        &AssemblerDialog::applyInstructionFilter);
    connect(
        instructionTreeWidget,
        &QTreeWidget::itemActivated,
        this,
        &AssemblerDialog::insertInstructionMnemonic);

    reloadInstructionDescriptions();
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

void AssemblerDialog::reloadInstructionDescriptions()
{
    const bool blocked = instructionTreeWidget->blockSignals(true);
    instructionTreeWidget->setSortingEnabled(false);
    instructionTreeWidget->clear();

    const QList<InstructionDescription> descriptions = instructionDescriptions();
    for (const InstructionDescription &description : descriptions) {
        auto *item = new QTreeWidgetItem(
            instructionTreeWidget, {description.mnemonic, description.description});
        item->setToolTip(0, description.mnemonic);
        item->setToolTip(1, description.description);
    }

    instructionTreeWidget->setSortingEnabled(true);
    instructionTreeWidget->sortByColumn(0, Qt::AscendingOrder);
    instructionTreeWidget->blockSignals(blocked);
    applyInstructionFilter();
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

QList<AssemblerDialog::InstructionDescription> AssemblerDialog::instructionDescriptions() const
{
    QList<InstructionDescription> descriptions;
    const QString suffix = temporaryEvalSuffix();
    const QString aodaOutput = Core()->cmdRaw(QStringLiteral("aoda%1").arg(suffix));
    const QStringList aodaLines = aodaOutput.split(QLatin1Char('\n'), IAITO_QT_SKIP_EMPTY_PARTS);

    for (const QString &line : aodaLines) {
        const int separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }

        const QString mnemonic = line.left(separator).trimmed();
        const QString description = line.mid(separator + 1).trimmed();
        if (!mnemonic.isEmpty()) {
            descriptions.append({mnemonic, description});
        }
    }

    if (!descriptions.isEmpty()) {
        return descriptions;
    }

    const QString aomdOutput = Core()->cmdRaw(QStringLiteral("aomd%1").arg(suffix));
    const QStringList aomdLines = aomdOutput.split(QLatin1Char('\n'), IAITO_QT_SKIP_EMPTY_PARTS);
    for (const QString &line : aomdLines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        int separator = -1;
        for (int i = 0; i < trimmed.size(); ++i) {
            if (trimmed.at(i).isSpace()) {
                separator = i;
                break;
            }
        }

        if (separator < 0) {
            descriptions.append({trimmed, QString()});
            continue;
        }

        const QString mnemonic = trimmed.left(separator).trimmed();
        const QString description = trimmed.mid(separator + 1).trimmed();
        if (!mnemonic.isEmpty()) {
            descriptions.append({mnemonic, description});
        }
    }

    return descriptions;
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

    reloadInstructionDescriptions();
    if (lastInputMode == InputMode::Bytes) {
        updateFromBytes();
    } else {
        updateFromAssembly();
    }
}

void AssemblerDialog::applyInstructionFilter()
{
    const QStringList terms = instructionFilterLineEdit->text()
                                  .simplified()
                                  .split(QLatin1Char(' '), IAITO_QT_SKIP_EMPTY_PARTS);

    for (int i = 0; i < instructionTreeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = instructionTreeWidget->topLevelItem(i);
        const QString searchable = item->text(0) + QLatin1Char(' ') + item->text(1);
        bool visible = true;
        for (const QString &term : terms) {
            if (!searchable.contains(term, Qt::CaseInsensitive)) {
                visible = false;
                break;
            }
        }
        item->setHidden(!visible);
    }
}

void AssemblerDialog::insertInstructionMnemonic(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)

    if (!item) {
        return;
    }

    QTextCursor cursor = assemblyTextEdit->textCursor();
    cursor.insertText(item->text(0));
    assemblyTextEdit->setTextCursor(cursor);
    assemblyTextEdit->setFocus();
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
