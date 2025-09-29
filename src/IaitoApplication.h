#ifndef IAITOAPPLICATION_H
#define IAITOAPPLICATION_H

#include <QApplication>
#include <QEvent>
#include <QList>
#include <QProxyStyle>

#include "core/MainWindow.h"

enum class AutomaticAnalysisLevel { Ask, None, AAA, AAAA, AAAAA };

struct IaitoCommandLineOptions
{
    QStringList args;
    AutomaticAnalysisLevel analLevel = AutomaticAnalysisLevel::Ask;
    InitialOptions fileOpenOptions;
    QString pythonHome;
    bool outputRedirectionEnabled = true;
    bool enableIaitoPlugins = true;
    bool enableR2Plugins = true;
    bool showWelcomeDialog = false;
};

class IaitoApplication : public QApplication
{
    Q_OBJECT

public:
    IaitoApplication(int &argc, char **argv);
    ~IaitoApplication();

    MainWindow *getMainWindow() { return mainWindow; }

    void launchNewInstance(const QStringList &args = {});

protected:
    bool event(QEvent *e);

private:
    /**
     * @brief Load and translations depending on Language settings
     * @return true on success
     */
    bool loadTranslations();
    /**
     * @brief Parse commandline options and store them in a structure.
     * @return false if options have error
     */
    bool parseCommandLineOptions();

private:
    bool m_FileAlreadyDropped;
    MainWindow *mainWindow;
    IaitoCommandLineOptions clOptions;
};

/**
 * @brief IaitoProxyStyle is used to force shortcuts displaying in context menu
 */
class IaitoProxyStyle : public QProxyStyle
{
    Q_OBJECT
public:
    /**
     * @brief it is enough to get notification about QMenu polishing to force
     * shortcut displaying
     */
    void polish(QWidget *widget) override;
};

#endif // IAITOAPPLICATION_H
