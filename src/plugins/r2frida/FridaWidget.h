#pragma once

#include "widgets/IaitoDockWidget.h"

class FridaSession;
class QLabel;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStandardItemModel;
class QSortFilterProxyModel;
class QTabWidget;
class QTreeView;

class FridaWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit FridaWidget(FridaSession *session, MainWindow *main);

    void showConnectDialog();

private:
    void refresh();
    void refreshTables();
    void appendConsole(const QString &text);
    void runConsole();
    void runEditor(bool eternalize);
    void loadText(const QString &command, QPlainTextEdit *view);
    void seekRuntime(quint64 runtimeVa);
    void showExports(const QString &moduleName);

    FridaSession *session = nullptr;
    QLabel *stateLabel = nullptr;
    QLabel *deviceLabel = nullptr;
    QLabel *targetLabel = nullptr;
    QLabel *slideLabel = nullptr;
    QPushButton *resumeButton = nullptr;
    QPushButton *detachButton = nullptr;
    QComboBox *addressMode = nullptr;
    QLineEdit *filter = nullptr;
    QStandardItemModel *moduleModel = nullptr;
    QStandardItemModel *mapModel = nullptr;
    QSortFilterProxyModel *moduleProxy = nullptr;
    QSortFilterProxyModel *mapProxy = nullptr;
    QTreeView *modules = nullptr;
    QTreeView *maps = nullptr;
    QPlainTextEdit *threads = nullptr;
    QPlainTextEdit *traces = nullptr;
    QPlainTextEdit *runtime = nullptr;
    QPlainTextEdit *editor = nullptr;
    QPlainTextEdit *console = nullptr;
    QLineEdit *consoleInput = nullptr;
    QTabWidget *tabs = nullptr;
    bool wasAttached = false;
};

quint64 fridaRuntimeAddress(FridaSession *session, quint64 addr);
void fridaTrace(FridaSession *session, quint64 addr);
void fridaBreakpoint(FridaSession *session, quint64 addr);
void fridaShowHookDialog(QWidget *parent, FridaSession *session, quint64 addr);
void fridaShowLiveHex(QWidget *parent, FridaSession *session, quint64 runtimeAddr);
