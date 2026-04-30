#include "SettingsDialog.h"
#include "ui_SettingsDialog.h"

#include "../R2PluginsDialog.h"
#include "AnalOptionsWidget.h"
#include "AppearanceOptionsWidget.h"
#include "AsmOptionsWidget.h"
#include "DebugOptionsWidget.h"
#include "GraphOptionsWidget.h"
#include "KeyboardOptionsWidget.h"
#include "WebserverOptionsWidget.h"

#include "../ScriptManagerWidget.h"
#include "PluginsOptionsWidget.h"

#include "SettingCategory.h"

#include "common/Configuration.h"
#include "common/Helpers.h"

#include <QDialogButtonBox>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
{
    setAttribute(Qt::WA_DeleteOnClose);
    ui->setupUi(this);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
    ui->logoSvgWidget->load(Config()->getLogoFile());

    QList<SettingCategory> prefs{

        {
            tr("Disassembly"), new AsmOptionsWidget(this), QIcon(":/img/icons/disas.svg")
            /*
                ,{
                    {
                        "Graph",
                        new GraphOptionsWidget(this),
                        QIcon(":/img/icons/graph.svg")
                    },
                }
            */
        },
        {tr("Graph"), new GraphOptionsWidget(this), QIcon(":/img/icons/graph.svg")},
        {tr("Debug"), new DebugOptionsWidget(this), QIcon(":/img/icons/bug.svg")},
        {tr("Appearance"), new AppearanceOptionsWidget(this), QIcon(":/img/icons/polar.svg")},
        {// we must deprecate all the iaito plugins, just use r2 ones
         tr("Plugins"),
         // new PluginsOptionsWidget(this),
         new R2PluginsDialog(this),
         QIcon(":/img/icons/plugins.svg")},
        {tr("Scripts"), new ScriptManagerWidget(this), QIcon(":/img/icons/initialization.svg")},
        {tr("Analysis"), new AnalOptionsWidget(this), QIcon(":/img/icons/cog_light.svg")},
        {tr("Keyboard"), new KeyboardOptionsWidget(this), QIcon(":/img/icons/download_black.svg")},
        {tr("Webserver"), new WebserverOptionsWidget(this), QIcon(":/img/icons/cloud.svg")}};

    for (auto &c : prefs) {
        c.addItem(*ui->configCategories, *ui->configPanel);
    }

    connect(ui->configCategories, &QTreeWidget::currentItemChanged, this, &SettingsDialog::changePage);
    connect(ui->saveButtons, &QDialogButtonBox::accepted, this, &QWidget::close);

    QTreeWidgetItem *defitem = ui->configCategories->topLevelItem(0);
    ui->configCategories->setCurrentItem(defitem, 0);

    connect(Config(), &Configuration::interfaceThemeChanged, this, &SettingsDialog::chooseThemeIcons);
    chooseThemeIcons();
}

SettingsDialog::~SettingsDialog() {}

void SettingsDialog::showSection(SettingsDialog::Section section)
{
    QString sectionName;
    switch (section) {
    case Section::Disassembly:
        sectionName = tr("Disassembly");
        break;
    case Section::Appearance:
        sectionName = tr("Appearance");
        break;
    case Section::Analysis:
        sectionName = tr("Analysis");
        break;
    }

    const QList<QTreeWidgetItem *> items
        = ui->configCategories->findItems(sectionName, Qt::MatchExactly | Qt::MatchRecursive, 0);
    if (!items.isEmpty()) {
        ui->configCategories->setCurrentItem(items.first(), 0);
    }
}

void SettingsDialog::changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
    if (!current)
        current = previous;

    int index = current->data(0, Qt::UserRole).toInt();

    if (index)
        ui->configPanel->setCurrentIndex(index - 1);
}

void SettingsDialog::chooseThemeIcons()
{
    // List of QActions which have alternative icons in different themes
    const QList<QPair<QString, QString>> kCategoryIconsNames{
        {QStringLiteral("Debug"), QStringLiteral("bug.svg")},
        {QStringLiteral("Assembly"), QStringLiteral("disas.svg")},
        {QStringLiteral("Graph"), QStringLiteral("graph.svg")},
        {QStringLiteral("Appearance"), QStringLiteral("polar.svg")},
        {QStringLiteral("Plugins"), QStringLiteral("plugins.svg")},
        {QStringLiteral("Initialization Script"), QStringLiteral("initialization.svg")},
        {QStringLiteral("Analysis"), QStringLiteral("cog_light.svg")},
    };
    QList<QPair<void *, QString>> supportedIconsNames;

    foreach (const auto &p, kCategoryIconsNames) {
        QList<QTreeWidgetItem *> items
            = ui->configCategories->findItems(p.first, Qt::MatchContains | Qt::MatchRecursive, 0);
        if (items.isEmpty()) {
            continue;
        }
        supportedIconsNames.append({items.first(), p.second});
    }

    // Set the correct icon for the QAction
    qhelpers::setThemeIcons(supportedIconsNames, [](void *obj, const QIcon &icon) {
        // here we have our setter functor, in this case it is almost
        // "unique" because it specified the column in `setIcon` call
        static_cast<QTreeWidgetItem *>(obj)->setIcon(0, icon);
    });
}
