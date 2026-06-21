#include "ShortcutsOptionsWidget.h"
#include "SettingsDialog.h"
#include "common/ShortcutManager.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
void limitToSingleKey(QKeySequenceEdit *edit)
{
    QObject::connect(
        edit, &QKeySequenceEdit::keySequenceChanged, edit, [edit](const QKeySequence &seq) {
            if (seq.count() > 1) {
                edit->setKeySequence(QKeySequence(seq[0]));
            }
        });
}
} // namespace

ShortcutsOptionsWidget::ShortcutsOptionsWidget(SettingsDialog *dialog)
    : QDialog(dialog)
{
    auto *layout = new QVBoxLayout(this);

    filterEdit = new QLineEdit(this);
    filterEdit->setPlaceholderText(tr("Filter shortcuts..."));
    filterEdit->setClearButtonEnabled(true);
    layout->addWidget(filterEdit);
    connect(filterEdit, &QLineEdit::textChanged, this, &ShortcutsOptionsWidget::applyFilter);

    tree = new QTreeWidget(this);
    tree->setColumnCount(2);
    tree->setHeaderLabels({tr("Action"), tr("Shortcut")});
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->setRootIsDecorated(true);
    layout->addWidget(tree, 1);

    conflictLabel = new QLabel(this);
    conflictLabel->setWordWrap(true);
    QPalette pal = conflictLabel->palette();
    pal.setColor(QPalette::WindowText, Qt::red);
    conflictLabel->setPalette(pal);
    conflictLabel->hide();
    layout->addWidget(conflictLabel);

    auto *commandGroup = new QGroupBox(tr("Custom Shortcuts"), this);
    auto *groupLayout = new QVBoxLayout(commandGroup);

    auto *addRow = new QHBoxLayout();
    addRow->addStretch(1);
    auto *addButton = new QToolButton(commandGroup);
    addButton->setText(QStringLiteral("+"));
    addButton->setToolTip(tr("Add a custom shortcut"));
    addRow->addWidget(addButton);
    groupLayout->addLayout(addRow);

    commandTable = new QTableWidget(0, 4, commandGroup);
    commandTable->setHorizontalHeaderLabels(
        {tr("Shortcut"), tr("Command"), tr("Needs input"), QString()});
    commandTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    commandTable->verticalHeader()->setVisible(false);
    commandTable->setSelectionMode(QAbstractItemView::NoSelection);
    groupLayout->addWidget(commandTable);

    layout->addWidget(commandGroup);

    connect(addButton, &QToolButton::clicked, this, &ShortcutsOptionsWidget::addCommandRow);

    auto *resetAll = new QPushButton(tr("Reset all to defaults"), this);
    layout->addWidget(resetAll);
    connect(resetAll, &QPushButton::clicked, this, [this]() {
        ShortcutMgr()->resetAll();
        rebuild();
    });

    rebuild();
    reloadCommandShortcuts();
}

ShortcutsOptionsWidget::~ShortcutsOptionsWidget() = default;

void ShortcutsOptionsWidget::rebuild()
{
    tree->clear();
    editors.clear();

    auto sm = ShortcutMgr();
    for (ShortcutScope scope : ShortcutManager::scopes()) {
        const QStringList ids = sm->idsForScope(scope);
        if (ids.isEmpty()) {
            continue;
        }
        auto *group = new QTreeWidgetItem(tree, {ShortcutManager::scopeName(scope)});
        group->setFirstColumnSpanned(true);
        group->setExpanded(true);
        for (const QString &id : ids) {
            auto *item = new QTreeWidgetItem(group, {sm->displayName(id)});

            auto *editorWidget = new QWidget(tree);
            auto *hl = new QHBoxLayout(editorWidget);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(2);

            auto *kse = new QKeySequenceEdit(editorWidget);
            kse->setKeySequence(sm->sequence(id));
            limitToSingleKey(kse);
            hl->addWidget(kse, 1);

            auto *clearBtn = new QToolButton(editorWidget);
            clearBtn->setText(QString::fromUtf8("\xe2\x9c\x95"));
            clearBtn->setToolTip(tr("Clear (disable) shortcut"));
            hl->addWidget(clearBtn);

            auto *resetBtn = new QToolButton(editorWidget);
            resetBtn->setText(QString::fromUtf8("\xe2\x86\xba"));
            resetBtn->setToolTip(tr("Reset to default"));
            hl->addWidget(resetBtn);

            tree->setItemWidget(item, 1, editorWidget);
            editors.insert(id, kse);

            connect(kse, &QKeySequenceEdit::editingFinished, this, [this, id, kse]() {
                ShortcutMgr()->setSequence(id, kse->keySequence());
                refreshConflicts();
            });
            connect(clearBtn, &QToolButton::clicked, this, [this, id, kse]() {
                ShortcutMgr()->setSequence(id, QKeySequence());
                kse->setKeySequence(QKeySequence());
                refreshConflicts();
            });
            connect(resetBtn, &QToolButton::clicked, this, [this, id, kse]() {
                ShortcutMgr()->resetToDefault(id);
                kse->setKeySequence(ShortcutMgr()->sequence(id));
                refreshConflicts();
            });
        }
    }

    refreshConflicts();
    applyFilter(filterEdit->text());
}

void ShortcutsOptionsWidget::refreshConflicts()
{
    const QList<ShortcutConflict> conflicts = ShortcutMgr()->conflicts();
    if (conflicts.isEmpty()) {
        conflictLabel->hide();
        return;
    }
    QStringList lines;
    for (const ShortcutConflict &c : conflicts) {
        lines << tr("\"%1\" conflicts with \"%2\" (%3)")
                     .arg(
                         ShortcutMgr()->displayName(c.idA),
                         ShortcutMgr()->displayName(c.idB),
                         c.sequence.toString(QKeySequence::NativeText));
    }
    conflictLabel->setText(
        tr("Conflicts:") + QStringLiteral("\n") + lines.join(QStringLiteral("\n")));
    conflictLabel->show();
}

void ShortcutsOptionsWidget::applyFilter(const QString &text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = tree->topLevelItem(i);
        int visibleChildren = 0;
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem *child = group->child(j);
            const bool match = needle.isEmpty()
                               || child->text(0).contains(needle, Qt::CaseInsensitive);
            child->setHidden(!match);
            if (match) {
                ++visibleChildren;
            }
        }
        group->setHidden(visibleChildren == 0);
    }
}

void ShortcutsOptionsWidget::reloadCommandShortcuts()
{
    loadingCommands = true;
    commandTable->setRowCount(0);
    const QVector<CommandShortcut> list = ShortcutMgr()->commandShortcuts();
    for (const CommandShortcut &cs : list) {
        const int row = commandTable->rowCount();
        commandTable->insertRow(row);

        auto *kse = new QKeySequenceEdit(commandTable);
        kse->setKeySequence(cs.key);
        limitToSingleKey(kse);
        commandTable->setCellWidget(row, 0, kse);
        connect(kse, &QKeySequenceEdit::editingFinished, this, [this]() { saveCommandShortcuts(); });

        auto *cmdEdit = new QLineEdit(commandTable);
        cmdEdit->setText(cs.command);
        cmdEdit->setPlaceholderText(tr("radare2 command"));
        commandTable->setCellWidget(row, 1, cmdEdit);
        connect(cmdEdit, &QLineEdit::editingFinished, this, [this]() { saveCommandShortcuts(); });

        auto *inputCell = new QWidget(commandTable);
        auto *inputLayout = new QHBoxLayout(inputCell);
        inputLayout->setContentsMargins(0, 0, 0, 0);
        inputLayout->setAlignment(Qt::AlignCenter);
        auto *needsInput = new QCheckBox(inputCell);
        needsInput->setChecked(cs.needsInput);
        needsInput->setToolTip(tr("Prompt for arguments when triggered"));
        inputLayout->addWidget(needsInput);
        commandTable->setCellWidget(row, 2, inputCell);
        connect(needsInput, &QCheckBox::toggled, this, [this]() { saveCommandShortcuts(); });

        auto *removeButton = new QToolButton(commandTable);
        removeButton->setText(QString::fromUtf8("\xe2\x9c\x95"));
        removeButton->setToolTip(tr("Remove"));
        commandTable->setCellWidget(row, 3, removeButton);
        connect(removeButton, &QToolButton::clicked, this, [this, removeButton]() {
            for (int r = 0; r < commandTable->rowCount(); ++r) {
                if (commandTable->cellWidget(r, 3) == removeButton) {
                    commandTable->removeRow(r);
                    break;
                }
            }
            saveCommandShortcuts();
        });
    }
    loadingCommands = false;
    refreshConflicts();
}

void ShortcutsOptionsWidget::addCommandRow()
{
    QVector<CommandShortcut> list = ShortcutMgr()->commandShortcuts();
    list.append(CommandShortcut{});
    ShortcutMgr()->setCommandShortcuts(list);
    reloadCommandShortcuts();
}

void ShortcutsOptionsWidget::saveCommandShortcuts()
{
    if (loadingCommands) {
        return;
    }
    QVector<CommandShortcut> list;
    for (int row = 0; row < commandTable->rowCount(); ++row) {
        auto *kse = qobject_cast<QKeySequenceEdit *>(commandTable->cellWidget(row, 0));
        auto *cmdEdit = qobject_cast<QLineEdit *>(commandTable->cellWidget(row, 1));
        auto *inputCell = commandTable->cellWidget(row, 2);
        auto *needsInput = inputCell ? inputCell->findChild<QCheckBox *>() : nullptr;
        if (!kse || !cmdEdit) {
            continue;
        }
        CommandShortcut cs;
        cs.key = kse->keySequence();
        cs.command = cmdEdit->text();
        cs.needsInput = needsInput && needsInput->isChecked();
        if (cs.key.isEmpty() && cs.command.trimmed().isEmpty()) {
            continue;
        }
        list.append(cs);
    }
    ShortcutMgr()->setCommandShortcuts(list);
    refreshConflicts();
}
