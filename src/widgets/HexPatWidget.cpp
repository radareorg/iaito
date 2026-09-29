#include "HexPatWidget.h"
#include "ui_HexPatWidget.h"

#include "common/Configuration.h"
#include "common/Helpers.h"
#include "core/Iaito.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QRegularExpression>

namespace {
enum Column { Name, Type, Address, Size, Value, ColumnCount };

bool isPluginPath(const QString &path)
{
    // hexpat splits arguments on whitespace and passes paths unquoted to r2's
    // test/cat commands. Quoting the outer command alone cannot protect them.
    static const QRegularExpression unsafe(QStringLiteral("[^\\p{L}\\p{N}_./:-]"));
    return !path.contains(unsafe);
}

bool filterItem(QTreeWidgetItem *item, const QString &text, bool parentMatches = false)
{
    bool matches = parentMatches || text.isEmpty();
    for (int column = 0; column < ColumnCount; ++column) {
        matches |= item->text(column).contains(text, Qt::CaseInsensitive);
    }
    bool visible = matches;
    for (int i = 0; i < item->childCount(); ++i) {
        visible |= filterItem(item->child(i), text, matches);
    }
    item->setHidden(!visible);
    if (visible && !text.isEmpty() && item->childCount()) {
        item->setExpanded(true);
    }
    return visible;
}
} // namespace

HexPatWidget::HexPatWidget(MainWindow *main)
    : IaitoDockWidget(main)
    , ui(new Ui::HexPatWidget)
{
    ui->setupUi(this);
    ui->patternsTree->setFont(Config()->getFont());
    qhelpers::setVerticalScrollMode(ui->patternsTree);
    ui->patternsTree->header()->setStretchLastSection(true);

    connect(ui->openButton, &QPushButton::clicked, this, &HexPatWidget::openPattern);
    connect(ui->reloadButton, &QPushButton::clicked, this, &HexPatWidget::evaluatePattern);
    connect(ui->fileEdit, &QLineEdit::returnPressed, this, &HexPatWidget::evaluatePattern);
    connect(ui->includeEdit, &QLineEdit::returnPressed, this, &HexPatWidget::evaluatePattern);
    connect(ui->includeButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(
            this, tr("Pattern include directory"), ui->includeEdit->text());
        if (!path.isEmpty()) {
            ui->includeEdit->setText(QDir::fromNativeSeparators(path));
        }
    });
    connect(ui->filterEdit, &QLineEdit::textChanged, this, &HexPatWidget::filterPatterns);
    connect(ui->patternsTree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        seekToPattern(item);
    });
    connect(ui->patternsTree, &QTreeWidget::customContextMenuRequested, this, [this](QPoint pos) {
        auto *item = ui->patternsTree->itemAt(pos);
        if (!item) {
            return;
        }
        QMenu menu(this);
        auto *seek = menu.addAction(tr("Seek to address"), this, [this, item]() {
            seekToPattern(item);
        });
        seek->setEnabled(item->data(Address, Qt::UserRole).isValid());
        menu.addAction(tr("Copy value"), this, [item]() {
            QApplication::clipboard()->setText(item->text(Value));
        });
        menu.addAction(tr("Copy row"), this, [item]() {
            QStringList fields;
            for (int column = 0; column < ColumnCount; ++column) {
                fields.append(item->text(column));
            }
            QApplication::clipboard()->setText(fields.join(QLatin1Char('\t')));
        });
        menu.exec(ui->patternsTree->viewport()->mapToGlobal(pos));
    });
    connect(Config(), &Configuration::fontsUpdated, this, [this]() {
        ui->patternsTree->setFont(Config()->getFont());
    });
    connect(Core(), &IaitoCore::refreshAll, this, &HexPatWidget::updateAvailability);
    updateAvailability();
}

HexPatWidget::~HexPatWidget() = default;

void HexPatWidget::updateAvailability()
{
    bool found = false;
    for (const QJsonValue &entry : Core()->cmdj("Lcj").array()) {
        if (entry.toObject().value("name").toString() == QLatin1String("hexpat")) {
            found = true;
            break;
        }
    }
    if (!found) {
        ui->patternsTree->clear();
        ui->statusLabel->setText(tr("Install the hexpat r2js plugin to load ImHex patterns."));
    } else if (!available) {
        ui->statusLabel->setText(
            tr("Open a .hexpat file to evaluate it against the current binary."));
    }
    available = found;
    ui->dockWidgetContents->setEnabled(available);
    toggleViewAction()->setEnabled(available);
    const QString hint = available ? tr("Load and explore ImHex pattern files")
                                   : tr("Requires the hexpat r2js plugin (r2hexpat)");
    toggleViewAction()->setToolTip(hint);
    toggleViewAction()->setStatusTip(hint);
}

void HexPatWidget::openPattern()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open ImHex pattern"),
        ui->fileEdit->text(),
        tr("ImHex patterns (*.hexpat *.pat);;All files (*)"));
    if (!path.isEmpty()) {
        ui->fileEdit->setText(QDir::fromNativeSeparators(path));
        evaluatePattern();
    }
}

void HexPatWidget::evaluatePattern()
{
    updateAvailability();
    if (!available) {
        return;
    }
    // Drop the previous result before validation so a failed reload never looks successful.
    ui->patternsTree->clear();
    const QFileInfo file(ui->fileEdit->text());
    if (!file.isFile() || !file.isReadable()) {
        ui->statusLabel->setText(tr("Choose a readable ImHex pattern file."));
        return;
    }
    const QString path = QDir::fromNativeSeparators(file.absoluteFilePath());
    const QString include = ui->includeEdit->text();
    const QString includePath = include.isEmpty() ? QString()
                                                  : QDir::fromNativeSeparators(
                                                        QFileInfo(include).absoluteFilePath());
    if (!isPluginPath(path) || !isPluginPath(includePath)) {
        ui->statusLabel->setText(
            tr("The hexpat plugin requires paths without spaces or command metacharacters. "
               "Use paths containing only letters, numbers, /, :, ., _, or -."));
        return;
    }
    if (!includePath.isEmpty() && !QFileInfo(includePath).isDir()) {
        ui->statusLabel->setText(tr("The include directory does not exist."));
        return;
    }

    ui->fileEdit->setText(path);
    QString command = QStringLiteral("hexpatj ");
    if (!includePath.isEmpty()) {
        command += QStringLiteral("-I %1 ").arg(includePath);
    }
    command += path;
    // r2js shares the core's JavaScript runtime; execute on the core/UI thread.
    const QJsonDocument result = QJsonDocument::fromJson(Core()->cmd(command).toUtf8());
    if (!result.isArray()) {
        ui->statusLabel->setText(
            tr("Pattern evaluation failed. See the Console for hexpat error details."));
        return;
    }
    const QJsonArray patterns = result.array();
    for (const QJsonValue &pattern : patterns) {
        addPattern(ui->patternsTree->invisibleRootItem(), pattern.toObject());
    }
    ui->patternsTree->expandToDepth(0);
    for (int column = 0; column < Value; ++column) {
        ui->patternsTree->resizeColumnToContents(column);
    }
    filterPatterns();
    ui->statusLabel->setText(
        patterns.isEmpty()
            ? tr("Evaluation completed without placed patterns.")
            : tr("%n top-level pattern(s). Double-click a field to seek to its address.",
                 nullptr,
                 patterns.size()));
}

void HexPatWidget::addPattern(QTreeWidgetItem *parent, const QJsonObject &pattern)
{
    auto *item = new QTreeWidgetItem(parent);
    item->setText(Name, pattern.value("name").toString());
    item->setText(Type, pattern.value("type").toString());
    item->setText(Size, pattern.value("size").toVariant().toString());
    const QVariant address = pattern.value("addr").toVariant();
    bool validAddress = false;
    const RVA addr = address.toULongLong(&validAddress);
    if (validAddress) {
        item->setText(Address, RAddressString(addr));
        // Heap/local patterns and custom sections do not refer to the binary's address space.
        if (!pattern.value("local").toBool() && pattern.value("section").toInt() == 0) {
            item->setData(Address, Qt::UserRole, QVariant::fromValue(addr));
        }
    }
    item->setText(
        Value,
        pattern.contains("repr") ? pattern.value("repr").toString()
                                 : pattern.value("value").toVariant().toString());
    if (pattern.contains("bits")) {
        item->setText(Size, tr("%1 bits").arg(pattern.value("bits").toInt()));
    }
    QString detail
        = tr("Kind: %1\nOffset: %2")
              .arg(pattern.value("kind").toString(), pattern.value("offset").toVariant().toString());
    if (pattern.contains("bitOffset")) {
        detail += tr("\nBit offset: %1").arg(pattern.value("bitOffset").toInt());
    }
    if (pattern.value("local").toBool()) {
        detail += tr("\nLocal pattern (no binary address)");
    } else if (pattern.value("section").toInt() != 0) {
        detail += tr("\nSection: %1 (no binary address)")
                      .arg(pattern.value("section").toVariant().toString());
    }
    for (int column = 0; column < ColumnCount; ++column) {
        item->setToolTip(column, item->text(column) + QLatin1Char('\n') + detail);
    }
    for (const char *key : {"children", "entries"}) {
        for (const QJsonValue &child : pattern.value(key).toArray()) {
            addPattern(item, child.toObject());
        }
    }
    if (pattern.value("pointee").isObject()) {
        addPattern(item, pattern.value("pointee").toObject());
    }
}

void HexPatWidget::seekToPattern(QTreeWidgetItem *item)
{
    if (item && item->data(Address, Qt::UserRole).isValid()) {
        Core()->seekAndShow(item->data(Address, Qt::UserRole).toULongLong());
    }
}

void HexPatWidget::filterPatterns()
{
    const QString text = ui->filterEdit->text();
    for (int i = 0; i < ui->patternsTree->topLevelItemCount(); ++i) {
        filterItem(ui->patternsTree->topLevelItem(i), text);
    }
}
