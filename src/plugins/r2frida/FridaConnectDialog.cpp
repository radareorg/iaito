#include "FridaConnectDialog.h"

#include "FridaSession.h"
#include "FridaUri.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTreeWidget>
#include <QVBoxLayout>

FridaConnectDialog::FridaConnectDialog(FridaSession *session, QWidget *parent)
    : QDialog(parent)
    , session(session)
{
    setWindowTitle(tr("Connect to target"));
    setMinimumSize(560, 480);

    auto *layout = new QVBoxLayout(this);
    auto *transportRow = new QHBoxLayout();
    transportGroup = new QButtonGroup(this);
    localButton = new QRadioButton(tr("Local"), this);
    usbButton = new QRadioButton(tr("USB"), this);
    remoteButton = new QRadioButton(tr("Remote"), this);
    localButton->setChecked(true);
    transportGroup->addButton(localButton, int(FridaTransport::Local));
    transportGroup->addButton(usbButton, int(FridaTransport::Usb));
    transportGroup->addButton(remoteButton, int(FridaTransport::Remote));
    transportRow->addWidget(new QLabel(tr("Connection"), this));
    transportRow->addWidget(localButton);
    transportRow->addWidget(usbButton);
    transportRow->addWidget(remoteButton);
    transportRow->addStretch();
    layout->addLayout(transportRow);

    remoteHost = new QLineEdit(this);
    remoteHost->setPlaceholderText(tr("10.0.0.3:9999"));
    remoteHost->setVisible(false);
    layout->addWidget(remoteHost);

    auto *deviceRow = new QHBoxLayout();
    devices = new QComboBox(this);
    auto *refreshDevicesButton = new QPushButton(tr("Refresh devices"), this);
    deviceRow->addWidget(new QLabel(tr("Device"), this));
    deviceRow->addWidget(devices, 1);
    deviceRow->addWidget(refreshDevicesButton);
    layout->addLayout(deviceRow);

    auto *kindRow = new QHBoxLayout();
    kindGroup = new QButtonGroup(this);
    appsButton = new QRadioButton(tr("Applications"), this);
    processButton = new QRadioButton(tr("Processes"), this);
    appsButton->setChecked(true);
    kindGroup->addButton(appsButton, int(FridaListKind::Applications));
    kindGroup->addButton(processButton, int(FridaListKind::Processes));
    kindRow->addWidget(new QLabel(tr("Target type"), this));
    kindRow->addWidget(appsButton);
    kindRow->addWidget(processButton);
    kindRow->addStretch();
    layout->addLayout(kindRow);

    search = new QLineEdit(this);
    search->setPlaceholderText(tr("Search"));
    layout->addWidget(search);

    targets = new QTreeWidget(this);
    targets->setHeaderLabels({tr("Name"), tr("Identifier"), tr("PID")});
    targets->setRootIsDecorated(false);
    targets->setAlternatingRowColors(true);
    targets->header()->setStretchLastSection(false);
    targets->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    targets->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(targets, 1);

    status = new QLabel(this);
    status->setWordWrap(true);
    layout->addWidget(status);

    auto *buttons = new QHBoxLayout();
    auto *attachButton = new QPushButton(tr("Attach"), this);
    auto *spawnButton = new QPushButton(tr("Spawn"), this);
    auto *launchButton = new QPushButton(tr("Launch"), this);
    auto *closeButton = new QPushButton(tr("Close"), this);
    buttons->addWidget(attachButton);
    buttons->addWidget(spawnButton);
    buttons->addWidget(launchButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    const auto transportChanged = [this](bool checked) {
        if (!checked) {
            return;
        }
        remoteHost->setVisible(transport() == FridaTransport::Remote);
        applyDevices(deviceCache);
        refreshTargets();
    };
    connect(localButton, &QRadioButton::toggled, this, transportChanged);
    connect(usbButton, &QRadioButton::toggled, this, transportChanged);
    connect(remoteButton, &QRadioButton::toggled, this, transportChanged);
    connect(appsButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            refreshTargets();
        }
    });
    connect(processButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            refreshTargets();
        }
    });
    connect(
        devices,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int) { refreshTargets(); });
    connect(refreshDevicesButton, &QPushButton::clicked, this, &FridaConnectDialog::refreshDevices);
    connect(search, &QLineEdit::textChanged, this, &FridaConnectDialog::filterTargets);
    connect(attachButton, &QPushButton::clicked, this, [this]() { choose(FridaAction::Attach); });
    connect(spawnButton, &QPushButton::clicked, this, [this]() { choose(FridaAction::Spawn); });
    connect(launchButton, &QPushButton::clicked, this, [this]() { choose(FridaAction::Launch); });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(targets, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) {
        const FridaTargetInfo target = selectedTarget();
        const bool running = target.pid != QStringLiteral("-") && !target.pid.isEmpty();
        choose(running ? FridaAction::Attach : FridaAction::Spawn);
    });

    if (!session->checkAvailable()) {
        status->setText(
            tr("r2frida is not loaded. Install it with r2pm -ci r2frida and restart iaito."));
        attachButton->setEnabled(false);
        spawnButton->setEnabled(false);
        launchButton->setEnabled(false);
        return;
    }
    refreshDevices();
}

FridaTransport FridaConnectDialog::transport() const
{
    return static_cast<FridaTransport>(transportGroup->checkedId());
}

FridaListKind FridaConnectDialog::listKind() const
{
    return static_cast<FridaListKind>(kindGroup->checkedId());
}

QString FridaConnectDialog::deviceId() const
{
    if (transport() == FridaTransport::Remote) {
        const QString host = remoteHost->text().trimmed();
        if (!host.isEmpty()) {
            return host;
        }
    }
    return devices->currentData().toString();
}

QString FridaConnectDialog::deviceLabel() const
{
    if (transport() == FridaTransport::Remote && !remoteHost->text().trimmed().isEmpty()) {
        return remoteHost->text().trimmed();
    }
    const QString text = devices->currentText();
    return text.isEmpty() ? deviceId() : text;
}

FridaTargetInfo FridaConnectDialog::selectedTarget() const
{
    FridaTargetInfo target;
    QTreeWidgetItem *item = targets->currentItem();
    if (!item) {
        return target;
    }
    target.name = item->text(0);
    target.identifier = item->text(1);
    target.pid = item->text(2);
    return target;
}

void FridaConnectDialog::refreshDevices()
{
    if (!session->checkAvailable()) {
        return;
    }
    status->setText(tr("Listing devices..."));
    const int generation = ++probeGeneration;
    session->probe(FridaUri::listDevices(), [this, generation](const QString &output) {
        if (generation != probeGeneration) {
            return;
        }
        deviceCache = FridaBackend::parseDevices(output);
        applyDevices(deviceCache);
        if (deviceCache.isEmpty()) {
            status->setText(
                output.trimmed().isEmpty() ? tr("No devices found.") : output.trimmed());
        }
        refreshTargets();
    });
}

void FridaConnectDialog::applyDevices(const QList<FridaDeviceInfo> &devices)
{
    const QString previous = deviceId();
    this->devices->blockSignals(true);
    this->devices->clear();
    const QString want = transport() == FridaTransport::Local
                             ? QStringLiteral("local")
                             : (transport() == FridaTransport::Usb ? QStringLiteral("usb")
                                                                   : QStringLiteral("remote"));
    for (const FridaDeviceInfo &device : devices) {
        if (device.type != want) {
            continue;
        }
        const QString label
            = device.name.isEmpty()
                  ? device.id
                  : device.name + QStringLiteral(" (") + device.id + QLatin1Char(')');
        this->devices->addItem(label, device.id);
    }
    const int index = this->devices->findData(previous);
    if (index >= 0) {
        this->devices->setCurrentIndex(index);
    }
    this->devices->blockSignals(false);
}

void FridaConnectDialog::refreshTargets()
{
    if (!session->checkAvailable() || session->busy()) {
        return;
    }
    status->setText(tr("Listing targets..."));
    targets->clear();
    const int generation = ++probeGeneration;
    const QString uri = FridaUri::listTargets(transport(), listKind(), deviceId());
    session->probe(uri, [this, generation](const QString &output) {
        if (generation != probeGeneration) {
            return;
        }
        const QList<FridaTargetInfo> parsed = FridaBackend::parseTargets(output, listKind());
        applyTargets(parsed);
        if (parsed.isEmpty()) {
            status->setText(
                output.trimmed().isEmpty() ? tr("No targets found.") : output.trimmed());
        } else {
            status->setText(tr("%1 targets").arg(parsed.size()));
        }
        filterTargets(search->text());
    });
}

void FridaConnectDialog::applyTargets(const QList<FridaTargetInfo> &targets)
{
    this->targets->clear();
    for (const FridaTargetInfo &target : targets) {
        auto *item = new QTreeWidgetItem(this->targets);
        item->setText(0, target.name);
        item->setText(1, target.identifier);
        item->setText(2, target.pid);
    }
}

void FridaConnectDialog::filterTargets(const QString &text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < targets->topLevelItemCount(); ++row) {
        QTreeWidgetItem *item = targets->topLevelItem(row);
        const bool match = needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive)
                           || item->text(1).contains(needle, Qt::CaseInsensitive)
                           || item->text(2).contains(needle, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void FridaConnectDialog::choose(FridaAction action)
{
    const FridaTargetInfo target = selectedTarget();
    QString id;
    if (action == FridaAction::Attach) {
        bool pidOk = false;
        const int pid = target.pid.toInt(&pidOk);
        if (pidOk && pid > 0) {
            id = target.pid;
        }
    }
    if (id.isEmpty()) {
        id = !target.identifier.isEmpty() ? target.identifier : target.name;
    }
    if (!FridaUri::targetIsSafe(id)) {
        QMessageBox::warning(
            this,
            tr("Connect to target"),
            tr("Select an application or process."));
        return;
    }
    if (transport() == FridaTransport::Remote && deviceId().isEmpty()) {
        QMessageBox::warning(this, tr("Connect to target"), tr("Enter a remote host:port."));
        return;
    }
    const QString uri = FridaUri::target(transport(), action, deviceId(), id);
    QString transportName = tr("local");
    if (transport() == FridaTransport::Usb) {
        transportName = tr("usb");
    } else if (transport() == FridaTransport::Remote) {
        transportName = tr("remote");
    }
    session->openTarget(uri, action == FridaAction::Spawn, deviceLabel(), transportName);
    accept();
}
