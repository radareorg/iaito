#pragma once

#include "widgets/IaitoDockWidget.h"

class FridaSession;
class QComboBox;
class QLabel;
class QPushButton;
class QWidget;

class FridaWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit FridaWidget(FridaSession *session, MainWindow *main);

    void showConnectDialog();

private:
    void refresh();
    void fillChoice(QComboBox *combo, const QString &current);
    void setLive(bool live);

    FridaSession *session = nullptr;
    QWidget *statusDot = nullptr;
    QLabel *stateLabel = nullptr;
    QLabel *pidLabel = nullptr;
    QLabel *archLabel = nullptr;
    QLabel *sessionLabel = nullptr;
    QComboBox *deviceCombo = nullptr;
    QComboBox *targetCombo = nullptr;
    QPushButton *connectButton = nullptr;
    QPushButton *resumeButton = nullptr;
    QPushButton *detachButton = nullptr;
    bool wasAttached = false;
};

quint64 fridaRuntimeAddress(FridaSession *session, quint64 addr);
void fridaSeekRuntime(FridaSession *session, quint64 runtimeVa);
void fridaTrace(FridaSession *session, quint64 addr);
void fridaBreakpoint(FridaSession *session, quint64 addr);
void fridaShowHookDialog(QWidget *parent, FridaSession *session, quint64 addr);
void fridaShowLiveHex(QWidget *parent, FridaSession *session, quint64 runtimeAddr);
void fridaShowExports(QWidget *parent, FridaSession *session, const QString &moduleName);
