#include "dialogs/AddressSpaceManagerDialog.h"

#include "core/Iaito.h"
#include "core/MainWindow.h"
#include "widgets/BinariesWidget.h"
#include "widgets/FilesWidget.h"
#include "widgets/FilesystemWidget.h"
#include "widgets/IaitoDockWidget.h"
#include "widgets/MapsWidget.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QSignalBlocker>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>

AddressSpaceManagerDialog::AddressSpaceManagerDialog(MainWindow *mainWindow)
    : QDialog(mainWindow)
    , tabs(new QTabWidget(this))
{
    setObjectName(QStringLiteral("addressSpaceManagerDialog"));
    setWindowTitle(tr("Manage Address Space"));
    resize(1000, 680);

    addDockTab(new FilesWidget(mainWindow), tr("Files"), QStringLiteral("AddressSpaceFilesWidget"));
    addDockTab(
        new FilesystemWidget(mainWindow),
        tr("FileSystem"),
        QStringLiteral("AddressSpaceFilesystemWidget"));
    addDockTab(new MapsWidget(mainWindow), tr("Maps"), QStringLiteral("AddressSpaceMapsWidget"));
    addDockTab(
        new BinariesWidget(mainWindow),
        tr("Binaries"),
        QStringLiteral("AddressSpaceBinariesWidget"));
    tabs->addTab(createOptionsTab(), tr("Options"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);

    connect(Core(), &IaitoCore::ioCacheChanged, this, &AddressSpaceManagerDialog::refreshOptions);
    connect(Core(), &IaitoCore::ioModeChanged, this, &AddressSpaceManagerDialog::refreshOptions);
}

AddressSpaceManagerDialog::~AddressSpaceManagerDialog() = default;

void AddressSpaceManagerDialog::addDockTab(
    IaitoDockWidget *dock, const QString &title, const QString &objectName)
{
    dock->setObjectName(objectName);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setAllowedAreas(Qt::NoDockWidgetArea);

    auto *titleBar = new QWidget(dock);
    titleBar->setFixedHeight(0);
    dock->setTitleBarWidget(titleBar);

    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(dock);
    tabs->addTab(page, title);
}

QWidget *AddressSpaceManagerDialog::createOptionsTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    const QStringList optionKeys = {
        QStringLiteral("io.cache"),
        QStringLiteral("io.pava"),
        QStringLiteral("io.va"),
        QStringLiteral("io.unalloc"),
    };

    for (const QString &key : optionKeys) {
        auto *checkBox = new QCheckBox(key, page);
        checkBox->setToolTip(Core()->getConfigDescription(key.toUtf8().constData()));
        options.push_back({key, checkBox});
        layout->addWidget(checkBox);

        connect(checkBox, &QCheckBox::toggled, this, [this, key](bool checked) {
            setOption(key, checked);
        });
    }

    layout->addStretch();
    refreshOptions();
    return page;
}

void AddressSpaceManagerDialog::refreshOptions()
{
    for (ConfigOption &option : options) {
        const QSignalBlocker blocker(option.checkBox);
        option.checkBox->setChecked(Core()->getConfigb(option.key));
    }
}

void AddressSpaceManagerDialog::setOption(const QString &key, bool enabled)
{
    if (key == QLatin1String("io.cache")) {
        Core()->setIOCache(enabled);
    } else {
        Core()->setConfig(key, enabled);
        Core()->triggerRefreshAll();
    }
    refreshOptions();
}
