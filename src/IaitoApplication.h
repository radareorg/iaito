#ifndef IAITOAPPLICATION_H
#define IAITOAPPLICATION_H

#include <QApplication>
#include <QEvent>
#include <QList>

#include "core/MainWindow.h"

enum class AutomaticAnalysisLevel { Ask, None, AAA, AAAA, AAAAA };

struct IaitoCommandLineOptions
{
    QStringList args;
    AutomaticAnalysisLevel analLevel = AutomaticAnalysisLevel::Ask;
    InitialOptions fileOpenOptions;
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
    bool notify(QObject *receiver, QEvent *event) override;

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

#endif // IAITOAPPLICATION_H
