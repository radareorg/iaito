#include "R2PluginsDialog.h"
#include "ui_R2PluginsDialog.h"

#include "common/Configuration.h"
#include "common/Helpers.h"
#include "core/Iaito.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QPushButton>
#include <QTreeWidgetItem>

namespace {

QString r2QuotedPluginFileArg(const QString &path)
{
    QString result = IaitoCore::sanitizeStringForCommand(path);
    result.replace(QLatin1Char('"'), QLatin1Char('_'));
    result.replace(QLatin1Char('\n'), QLatin1Char('_'));
    result.replace(QLatin1Char('\r'), QLatin1Char('_'));
    return QStringLiteral("\"%1\"").arg(result);
}

} // namespace

R2PluginsDialog::R2PluginsDialog(QWidget *parent, bool showDialogButtons)
    : QDialog(parent)
    , ui(new Ui::R2PluginsDialog)
{
    ui->setupUi(this);

    if (showDialogButtons) {
        ui->buttonBox->setStandardButtons(QDialogButtonBox::Ok);
        QPushButton *loadPluginButton
            = ui->buttonBox->addButton(tr("Load plugin"), QDialogButtonBox::ActionRole);
        connect(loadPluginButton, &QPushButton::clicked, this, &R2PluginsDialog::loadPlugin);
    } else {
        ui->buttonBox->hide();
    }

    refreshPluginDescriptions();
}

R2PluginsDialog::~R2PluginsDialog()
{
    delete ui;
}

void R2PluginsDialog::loadPlugin()
{
    const QString nativeExt = QStringLiteral(R_LIB_EXT);
    const QString filter
        = tr("radare2 plugins (*.%1 *.r2.js);;Native plugins (*.so *.dll *.dylib);;"
             "r2js plugins (*.r2.js);;All files (*)")
              .arg(nativeExt);
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Load radare2 plugin"),
        Config()->getRecentFolder(),
        filter,
        nullptr,
        QFILEDIALOG_FLAGS);
    if (fileName.isEmpty()) {
        return;
    }

    Config()->setRecentFolder(QFileInfo(fileName).absolutePath());
    const bool loaded = Core()->cmdRaw0(QStringLiteral("L %1").arg(r2QuotedPluginFileArg(fileName)));
    if (loaded) {
        Core()->message(tr("Loaded plugin: %1").arg(fileName));
        refreshPluginDescriptions();
    } else {
        Core()->message(tr("Failed to load plugin: %1").arg(fileName));
    }
}

void R2PluginsDialog::refreshPluginDescriptions()
{
    ui->RBinTreeWidget->clear();
    for (const auto &plugin : Core()->getRBinPluginDescriptions()) {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, plugin.name);
        item->setText(1, plugin.description);
        item->setText(2, plugin.license);
        item->setText(3, plugin.type);
        ui->RBinTreeWidget->addTopLevelItem(item);
    }
    ui->RBinTreeWidget->sortByColumn(0, Qt::AscendingOrder);
    qhelpers::adjustColumns(ui->RBinTreeWidget, 0);

    ui->RIOTreeWidget->clear();
    for (const auto &plugin : Core()->getRIOPluginDescriptions()) {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, plugin.name);
        item->setText(1, plugin.description);
        item->setText(2, plugin.license);
        item->setText(3, plugin.permissions);
        ui->RIOTreeWidget->addTopLevelItem(item);
    }
    ui->RIOTreeWidget->sortByColumn(0, Qt::AscendingOrder);
    qhelpers::adjustColumns(ui->RIOTreeWidget, 0);

    ui->RCoreTreeWidget->clear();
    for (const auto &plugin : Core()->getRCorePluginDescriptions()) {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, plugin.name);
        item->setText(1, plugin.description);
        ui->RCoreTreeWidget->addTopLevelItem(item);
    }
    ui->RCoreTreeWidget->sortByColumn(0, Qt::AscendingOrder);
    qhelpers::adjustColumns(ui->RCoreTreeWidget, 0);

    ui->RAsmTreeWidget->clear();
    for (const auto &plugin : Core()->getRAsmPluginDescriptions()) {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, plugin.name);
        item->setText(1, plugin.architecture);
        item->setText(2, plugin.cpus);
        item->setText(3, plugin.version);
        item->setText(4, plugin.description);
        item->setText(5, plugin.license);
        item->setText(6, plugin.author);
        ui->RAsmTreeWidget->addTopLevelItem(item);
    }
    ui->RAsmTreeWidget->sortByColumn(0, Qt::AscendingOrder);
    qhelpers::adjustColumns(ui->RAsmTreeWidget, 0);

    // RAnal

    ui->RAnalTreeWidget->clear();
    for (const auto &plugin : Core()->getRAnalPluginDescriptions()) {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, plugin.name);
        item->setText(1, plugin.architecture);
        item->setText(2, plugin.version);
        item->setText(3, plugin.description);
        item->setText(4, plugin.license);
        item->setText(5, plugin.author);
        item->setText(6, plugin.cpus);
        ui->RAnalTreeWidget->addTopLevelItem(item);
    }
    ui->RAnalTreeWidget->sortByColumn(0, Qt::AscendingOrder);
    qhelpers::adjustColumns(ui->RAnalTreeWidget, 0);

    // RMuta

    ui->RMutaTreeWidget->clear();
    for (const auto &plugin : Core()->getRMutaPluginDescriptions()) {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, plugin.name);
        item->setText(1, plugin.type);
        item->setText(2, plugin.description);
        item->setText(3, plugin.license);
        item->setText(4, plugin.author);
        ui->RMutaTreeWidget->addTopLevelItem(item);
    }
    ui->RMutaTreeWidget->sortByColumn(0, Qt::AscendingOrder);
    qhelpers::adjustColumns(ui->RMutaTreeWidget, 0);

    // RLang

    ui->RLangTreeWidget->clear();
    for (const auto &plugin : Core()->getRLangPluginDescriptions()) {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, plugin.name);
        item->setText(1, plugin.description);
        item->setText(2, plugin.license);
        item->setText(3, plugin.alias);
        item->setText(4, plugin.ext);
        ui->RLangTreeWidget->addTopLevelItem(item);
    }
    ui->RLangTreeWidget->sortByColumn(0, Qt::AscendingOrder);
    qhelpers::adjustColumns(ui->RLangTreeWidget, 0);

    // RFS

    ui->RFSTreeWidget->clear();
    for (const auto &plugin : Core()->getRFSPluginDescriptions()) {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, plugin.name);
        item->setText(1, plugin.description);
        item->setText(2, plugin.license);
        item->setText(3, plugin.author);
        ui->RFSTreeWidget->addTopLevelItem(item);
    }
    ui->RFSTreeWidget->sortByColumn(0, Qt::AscendingOrder);
    qhelpers::adjustColumns(ui->RFSTreeWidget, 0);

    // RDebug

    ui->RDebugTreeWidget->clear();
    for (const auto &plugin : Core()->getRDebugPluginDescriptions()) {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, plugin.name);
        item->setText(1, plugin.description);
        item->setText(2, plugin.arch);
        item->setText(3, plugin.license);
        item->setText(4, plugin.author);
        ui->RDebugTreeWidget->addTopLevelItem(item);
    }
    ui->RDebugTreeWidget->sortByColumn(0, Qt::AscendingOrder);
    qhelpers::adjustColumns(ui->RDebugTreeWidget, 0);
}
