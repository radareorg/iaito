#include "R2PluginsDialog.h"
#include "ui_R2PluginsDialog.h"

#include "common/Helpers.h"
#include "core/Iaito.h"
#include "plugins/PluginManager.h"

R2PluginsDialog::R2PluginsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::R2PluginsDialog)
{
    ui->setupUi(this);

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

    for (const auto &plugin : Core()->getRCorePluginDescriptions()) {
        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, plugin.name);
        item->setText(1, plugin.description);
        ui->RCoreTreeWidget->addTopLevelItem(item);
    }
    ui->RCoreTreeWidget->sortByColumn(0, Qt::AscendingOrder);
    qhelpers::adjustColumns(ui->RCoreTreeWidget, 0);

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
}

R2PluginsDialog::~R2PluginsDialog()
{
    delete ui;
}
