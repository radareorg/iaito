#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "IaitoSamplePlugin.h"
#include "MainWindow.h"
#include "common/Configuration.h"
#include "common/TempConfig.h"

void IaitoSamplePlugin::setupPlugin() {}

void IaitoSamplePlugin::setupInterface(MainWindow *main)
{
    IaitoSamplePluginWidget *widget = new IaitoSamplePluginWidget(main);
    main->addPluginDockWidget(widget);
}

IaitoSamplePluginWidget::IaitoSamplePluginWidget(MainWindow *main)
    : IaitoDockWidget(main)
{
    this->setObjectName("IaitoSamplePluginWidget");
    this->setWindowTitle("Sample C++ Plugin");
    QWidget *content = new QWidget();
    this->setWidget(content);

    QVBoxLayout *layout = new QVBoxLayout(content);
    content->setLayout(layout);
    text = new QLabel(content);
    text->setFont(Config()->getFont());
    text->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    layout->addWidget(text);

    QPushButton *button = new QPushButton(content);
    button->setText("Want a fortune?");
    button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    button->setMaximumHeight(50);
    button->setMaximumWidth(200);
    layout->addWidget(button);
    layout->setAlignment(button, Qt::AlignHCenter);

    connect(Core(), &IaitoCore::seekChanged, this, &IaitoSamplePluginWidget::on_seekChanged);
    connect(button, &QPushButton::clicked, this, &IaitoSamplePluginWidget::on_buttonClicked);
}

void IaitoSamplePluginWidget::on_seekChanged(RVA addr)
{
    Q_UNUSED(addr);
    QString res;
    {
        TempConfig tempConfig;
        tempConfig.set("scr.color", 0);
        res = Core()->cmd("?E `pi 1`");
    }
    text->setText(res);
}

void IaitoSamplePluginWidget::on_buttonClicked()
{
    QString fortune = Core()->cmd("fo").replace("\n", "");
    // cmdRaw can be used to execute single raw commands
    // this is especially good for user-controlled input
    QString res = Core()->cmdRaw("?E " + fortune);
    text->setText(res);
}
