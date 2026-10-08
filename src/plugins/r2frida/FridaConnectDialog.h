#pragma once

#include "FridaTypes.h"

#include <QDialog>
#include <QList>

class FridaSession;
class QButtonGroup;
class QComboBox;
class QLineEdit;
class QLabel;
class QTreeWidget;
class QRadioButton;

class FridaConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FridaConnectDialog(FridaSession *session, QWidget *parent = nullptr);

private:
    void refreshDevices();
    void refreshTargets();
    void applyDevices(const QList<FridaDeviceInfo> &devices);
    void applyTargets(const QList<FridaTargetInfo> &targets);
    void filterTargets(const QString &text);
    void choose(FridaAction action);
    FridaTransport transport() const;
    FridaListKind listKind() const;
    QString deviceId() const;
    QString deviceLabel() const;
    FridaTargetInfo selectedTarget() const;

    FridaSession *session = nullptr;
    QButtonGroup *transportGroup = nullptr;
    QButtonGroup *kindGroup = nullptr;
    QRadioButton *localButton = nullptr;
    QRadioButton *usbButton = nullptr;
    QRadioButton *remoteButton = nullptr;
    QRadioButton *appsButton = nullptr;
    QRadioButton *processButton = nullptr;
    QLineEdit *remoteHost = nullptr;
    QComboBox *devices = nullptr;
    QLineEdit *search = nullptr;
    QTreeWidget *targets = nullptr;
    QLabel *status = nullptr;
    QList<FridaDeviceInfo> deviceCache;
    int probeGeneration = 0;
};
