#ifndef IAITOSAMPLEPLUGIN_H
#define IAITOSAMPLEPLUGIN_H

#include "IaitoPlugin.h"
#include <QObject>
#include <QtPlugin>

class IaitoSamplePlugin : public QObject, IaitoPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.radare.cutter.plugins.IaitoPlugin")
    Q_INTERFACES(IaitoPlugin)

public:
    void setupPlugin() override;
    void setupInterface(MainWindow *main) override;

    QString getName() const override { return "SamplePlugin"; }
    QString getAuthor() const override { return "someone"; }
    QString getDescription() const override { return "Just a sample plugin."; }
    QString getVersion() const override { return "1.0"; }
};

class IaitoSamplePluginWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit IaitoSamplePluginWidget(MainWindow *main, QAction *action);

private:
    QLabel *text;

private slots:
    void on_seekChanged(RVA addr);
    void on_buttonClicked();
};

#endif // IAITOSAMPLEPLUGIN_H
