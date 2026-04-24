#include "widgets/SyscallsWidget.h"

#include "common/Helpers.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace {
enum SyscallColumns {
    NameColumn,
    NumberColumn,
    VectorColumn,
    ArgsColumn,
    FormatColumn,
    ColumnCount
};

constexpr int SortRole = Qt::UserRole + 2;

class SortTableWidgetItem : public QTableWidgetItem
{
public:
    explicit SortTableWidgetItem(const QString &text)
        : QTableWidgetItem(text)
    {
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        const QVariant left = data(SortRole);
        const QVariant right = other.data(SortRole);
        if (left.isValid() && right.isValid()) {
            return left.toULongLong() < right.toULongLong();
        }
        return QTableWidgetItem::operator<(other);
    }
};
}

SyscallsWidget::SyscallsWidget(MainWindow *main)
    : IaitoDockWidget(main)
{
    setObjectName(
        main ? main->getUniqueObjectName(QStringLiteral("SyscallsWidget"))
             : QStringLiteral("SyscallsWidget"));
    setWindowTitle(tr("Syscalls"));
    toggleViewAction()->setText(tr("Syscalls"));

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);

    auto *configLayout = new QGridLayout;
    overrideCheckBox = new QCheckBox(tr("Override session defaults"), container);
    archComboBox = new QComboBox(container);
    bitsComboBox = new QComboBox(container);
    osComboBox = new QComboBox(container);
    auto *refreshButton = new QPushButton(tr("Refresh"), container);

    configLayout->addWidget(overrideCheckBox, 0, 0, 1, 4);
    configLayout->addWidget(new QLabel(tr("Arch"), container), 1, 0);
    configLayout->addWidget(archComboBox, 1, 1);
    configLayout->addWidget(new QLabel(tr("Bits"), container), 1, 2);
    configLayout->addWidget(bitsComboBox, 1, 3);
    configLayout->addWidget(new QLabel(tr("OS"), container), 1, 4);
    configLayout->addWidget(osComboBox, 1, 5);
    configLayout->addWidget(refreshButton, 1, 6);
    configLayout->setColumnStretch(1, 1);
    configLayout->setColumnStretch(3, 1);
    configLayout->setColumnStretch(5, 1);

    auto *filterLayout = new QHBoxLayout;
    filterLineEdit = new QLineEdit(container);
    filterLineEdit->setPlaceholderText(tr("Filter syscalls"));
    summaryLabel = new QLabel(container);
    filterLayout->addWidget(filterLineEdit, 1);
    filterLayout->addWidget(summaryLabel);

    syscallsTable = new QTableWidget(container);
    syscallsTable->setColumnCount(ColumnCount);
    syscallsTable->setHorizontalHeaderLabels(
        {tr("Name"), tr("Number"), tr("Vector"), tr("Args"), tr("Format")});
    syscallsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    syscallsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    syscallsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    syscallsTable->setSortingEnabled(true);
    syscallsTable->verticalHeader()->hide();
    syscallsTable->horizontalHeader()->setStretchLastSection(true);
    syscallsTable->horizontalHeader()->setSectionResizeMode(
        NameColumn, QHeaderView::Stretch);
    syscallsTable->horizontalHeader()->setSectionResizeMode(
        NumberColumn, QHeaderView::ResizeToContents);
    syscallsTable->horizontalHeader()->setSectionResizeMode(
        VectorColumn, QHeaderView::ResizeToContents);
    syscallsTable->horizontalHeader()->setSectionResizeMode(
        ArgsColumn, QHeaderView::ResizeToContents);

    detailTextEdit = new QPlainTextEdit(container);
    detailTextEdit->setReadOnly(true);
    detailTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    detailTextEdit->setPlaceholderText(tr("Select a syscall to run as <number>"));
    detailTextEdit->setMinimumHeight(detailTextEdit->fontMetrics().height() * 4);

    qhelpers::bindFont(syscallsTable, true, true);
    qhelpers::bindFont(detailTextEdit, false, true);

    layout->addLayout(configLayout);
    layout->addLayout(filterLayout);
    layout->addWidget(syscallsTable, 1);
    layout->addWidget(detailTextEdit);
    setWidget(container);

    populateConfigCombos();
    updateConfigWidgets();

    connect(overrideCheckBox, &QCheckBox::toggled, this, &SyscallsWidget::updateConfigWidgets);
    connect(overrideCheckBox, &QCheckBox::toggled, this, &SyscallsWidget::refreshSyscalls);
    connect(refreshButton, &QPushButton::clicked, this, &SyscallsWidget::refreshSyscalls);
    connect(filterLineEdit, &QLineEdit::textChanged, this, &SyscallsWidget::applyFilter);
    connect(syscallsTable, &QTableWidget::itemClicked, this, &SyscallsWidget::showSyscallDetails);
    connect(archComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (overrideCheckBox->isChecked()) {
            refreshSyscalls();
        }
    });
    connect(bitsComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (overrideCheckBox->isChecked()) {
            refreshSyscalls();
        }
    });
    connect(osComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (overrideCheckBox->isChecked()) {
            refreshSyscalls();
        }
    });

    refreshSyscalls();
}

QWidget *SyscallsWidget::widgetToFocusOnRaise()
{
    return filterLineEdit;
}

void SyscallsWidget::populateConfigCombos()
{
    auto setItems = [](QComboBox *combo, const QStringList &items) {
        QSignalBlocker blocker(combo);
        combo->clear();
        for (const QString &item : items) {
            const QString value = item.trimmed();
            if (!value.isEmpty()) {
                combo->addItem(value);
            }
        }
    };

    setItems(archComboBox, Core()->cmd("e asm.arch=?").split('\n', IAITO_QT_SKIP_EMPTY_PARTS));
    setItems(osComboBox, Core()->cmd("e asm.os=?").split('\n', IAITO_QT_SKIP_EMPTY_PARTS));
    setItems(bitsComboBox, {QStringLiteral("8"), QStringLiteral("16"), QStringLiteral("32"),
                            QStringLiteral("64")});
    syncConfigFromSession();
}

void SyscallsWidget::syncConfigFromSession()
{
    auto selectText = [](QComboBox *combo, const QString &text) {
        QSignalBlocker blocker(combo);
        const int index = combo->findText(text);
        if (index >= 0) {
            combo->setCurrentIndex(index);
        } else if (!text.isEmpty()) {
            combo->insertItem(0, text);
            combo->setCurrentIndex(0);
        }
    };

    selectText(archComboBox, Core()->cmd("e asm.arch").trimmed());
    selectText(bitsComboBox, Core()->cmd("e asm.bits").trimmed());
    selectText(osComboBox, Core()->cmd("e asm.os").trimmed());
}

void SyscallsWidget::updateConfigWidgets()
{
    const bool enabled = overrideCheckBox->isChecked();
    if (!enabled) {
        syncConfigFromSession();
    }
    archComboBox->setEnabled(enabled);
    bitsComboBox->setEnabled(enabled);
    osComboBox->setEnabled(enabled);
}

QString SyscallsWidget::sanitizeConfigValue(QString value)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_.-]+$"));
    value = value.trimmed();
    return valid.match(value).hasMatch() ? value : QString();
}

QString SyscallsWidget::selectedArch() const
{
    return sanitizeConfigValue(archComboBox->currentText());
}

QString SyscallsWidget::selectedBits() const
{
    return sanitizeConfigValue(bitsComboBox->currentText());
}

QString SyscallsWidget::selectedOs() const
{
    return sanitizeConfigValue(osComboBox->currentText());
}

QString SyscallsWidget::commandSuffix() const
{
    if (!overrideCheckBox->isChecked()) {
        return QString();
    }

    const QString arch = selectedArch();
    const QString bits = selectedBits();
    const QString os = selectedOs();
    if (arch.isEmpty() || bits.isEmpty() || os.isEmpty()) {
        return QString();
    }

    return QStringLiteral(" @e:asm.arch=%1 @e:asm.bits=%2 @e:asm.os=%3").arg(arch, bits, os);
}

bool SyscallsWidget::parseInteger(const QString &text, quint64 *value)
{
    bool ok = false;
    const quint64 result = text.trimmed().toULongLong(&ok, 0);
    if (ok && value) {
        *value = result;
    }
    return ok;
}

QString SyscallsWidget::numberText(quint64 value)
{
    return QString::number(value);
}

QList<SyscallsWidget::SyscallInfo> SyscallsWidget::parseSyscalls(const QString &output) const
{
    QList<SyscallInfo> result;
    const QStringList lines = output.split('\n', IAITO_QT_SKIP_EMPTY_PARTS);
    static const QRegularExpression syscallName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        const int equalIndex = line.indexOf(QLatin1Char('='));
        if (equalIndex <= 0) {
            continue;
        }

        const QString name = line.left(equalIndex).trimmed();
        if (!syscallName.match(name).hasMatch() || name == QLatin1String("_")) {
            continue;
        }

        const QString value = line.mid(equalIndex + 1).trimmed();
        const QStringList parts = value.split(QLatin1Char(','), Qt::KeepEmptyParts);
        if (parts.size() < 2) {
            continue;
        }

        SyscallInfo syscall;
        syscall.name = name;
        syscall.vectorText = parts.at(0).trimmed();
        if (!parseInteger(syscall.vectorText, &syscall.vector)
            || !parseInteger(parts.at(1), &syscall.number)) {
            continue;
        }

        if (parts.size() >= 3) {
            bool ok = false;
            const int argc = parts.at(2).trimmed().toInt(&ok);
            if (ok) {
                syscall.argc = argc;
            }
        }
        if (parts.size() >= 4) {
            QStringList formatParts = parts.mid(3);
            syscall.format = formatParts.join(QLatin1Char(',')).trimmed();
        }
        result.append(syscall);
    }

    std::sort(result.begin(), result.end(), [](const SyscallInfo &a, const SyscallInfo &b) {
        if (a.number == b.number) {
            return a.name < b.name;
        }
        return a.number < b.number;
    });
    return result;
}

QList<SyscallsWidget::SyscallInfo> SyscallsWidget::parseSyscallList(const QString &output) const
{
    QList<SyscallInfo> result;
    const QStringList lines = output.split('\n', IAITO_QT_SKIP_EMPTY_PARTS);
    static const QRegularExpression syscallName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        const int equalIndex = line.indexOf(QLatin1Char('='));
        if (equalIndex <= 0) {
            continue;
        }

        const QString name = line.left(equalIndex).trimmed();
        if (!syscallName.match(name).hasMatch()) {
            continue;
        }

        const QString vectorAndNumber = line.mid(equalIndex + 1).trimmed();
        const int dotIndex = vectorAndNumber.lastIndexOf(QLatin1Char('.'));
        if (dotIndex <= 0) {
            continue;
        }

        SyscallInfo syscall;
        syscall.name = name;
        syscall.vectorText = vectorAndNumber.left(dotIndex);
        if (!parseInteger(syscall.vectorText, &syscall.vector)
            || !parseInteger(vectorAndNumber.mid(dotIndex + 1), &syscall.number)) {
            continue;
        }
        result.append(syscall);
    }

    std::sort(result.begin(), result.end(), [](const SyscallInfo &a, const SyscallInfo &b) {
        if (a.number == b.number) {
            return a.name < b.name;
        }
        return a.number < b.number;
    });
    return result;
}

void SyscallsWidget::refreshSyscalls()
{
    if (!overrideCheckBox->isChecked()) {
        syncConfigFromSession();
    }

    const QString suffix = commandSuffix();
    const QString listOutput = Core()->cmd(QStringLiteral("asl%1").arg(suffix));
    const QString formatOutput = Core()->cmd(QStringLiteral("ask%1").arg(suffix));
    QList<SyscallInfo> syscalls = parseSyscallList(listOutput);
    const QList<SyscallInfo> formatSyscalls = parseSyscalls(formatOutput);
    QHash<QString, SyscallInfo> formatByName;
    for (const SyscallInfo &syscall : formatSyscalls) {
        formatByName.insert(syscall.name, syscall);
    }

    for (SyscallInfo &syscall : syscalls) {
        const SyscallInfo formatInfo = formatByName.value(syscall.name);
        if (formatInfo.name.isEmpty()) {
            continue;
        }
        syscall.argc = formatInfo.argc;
        syscall.format = formatInfo.format;
    }
    if (syscalls.isEmpty()) {
        syscalls = formatSyscalls;
    }

    syscallsTable->setSortingEnabled(false);
    syscallsTable->clearContents();
    syscallsTable->setRowCount(syscalls.size());

    int row = 0;
    for (const SyscallInfo &syscall : syscalls) {
        auto *nameItem = new QTableWidgetItem(syscall.name);
        auto *numberItem = new SortTableWidgetItem(numberText(syscall.number));
        auto *vectorItem = new SortTableWidgetItem(syscall.vectorText);
        auto *argsItem = new SortTableWidgetItem(
            syscall.argc >= 0 ? QString::number(syscall.argc) : QString());
        auto *formatItem = new QTableWidgetItem(syscall.format);

        QList<QTableWidgetItem *> items = {nameItem, numberItem, vectorItem, argsItem, formatItem};
        for (QTableWidgetItem *item : items) {
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(syscall.number));
            item->setData(Qt::UserRole + 1, QVariant::fromValue<qulonglong>(syscall.vector));
        }
        numberItem->setData(SortRole, QVariant::fromValue<qulonglong>(syscall.number));
        vectorItem->setData(SortRole, QVariant::fromValue<qulonglong>(syscall.vector));
        if (syscall.argc >= 0) {
            argsItem->setData(SortRole, syscall.argc);
        }

        syscallsTable->setItem(row, NameColumn, nameItem);
        syscallsTable->setItem(row, NumberColumn, numberItem);
        syscallsTable->setItem(row, VectorColumn, vectorItem);
        syscallsTable->setItem(row, ArgsColumn, argsItem);
        syscallsTable->setItem(row, FormatColumn, formatItem);
        row++;
    }

    syscallsTable->setSortingEnabled(true);
    syscallsTable->sortItems(NumberColumn, Qt::AscendingOrder);
    applyFilter(filterLineEdit->text());

    summaryLabel->setText(
        tr("%1 syscalls for %2/%3/%4")
            .arg(syscalls.size())
            .arg(archComboBox->currentText(),
                 bitsComboBox->currentText(),
                 osComboBox->currentText()));
    detailTextEdit->clear();
}

void SyscallsWidget::showSyscallDetails(QTableWidgetItem *item)
{
    if (!item) {
        return;
    }

    const qulonglong number = item->data(Qt::UserRole).toULongLong();
    const QString command = QStringLiteral("as %1%2").arg(number).arg(commandSuffix());
    const QString output = Core()->cmd(command).trimmed();
    detailTextEdit->setPlainText(QStringLiteral("> %1\n%2").arg(command, output));
}

void SyscallsWidget::applyFilter(const QString &text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < syscallsTable->rowCount(); row++) {
        bool match = needle.isEmpty();
        for (int column = 0; !match && column < syscallsTable->columnCount(); column++) {
            QTableWidgetItem *item = syscallsTable->item(row, column);
            match = item && item->text().contains(needle, Qt::CaseInsensitive);
        }
        syscallsTable->setRowHidden(row, !match);
    }
}
