// PackageManagerDialog.cpp
#include "PackageManagerDialog.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVBoxLayout>

void CheckBoxDelegate::paint(
    QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionButton checkboxOption;
    checkboxOption.rect = option.rect.adjusted(2, 2, -2, -2);
    checkboxOption.state = QStyle::State_Enabled;
    bool checked = index.data(Qt::DisplayRole).toString() == "1";
    if (checked) {
        checkboxOption.state |= QStyle::State_On;
    } else {
        checkboxOption.state |= QStyle::State_Off;
    }
    QApplication::style()->drawControl(QStyle::CE_CheckBox, &checkboxOption, painter);
}

PackageManagerDialog::PackageManagerDialog(QWidget *parent)
    : QDialog(parent)
    , m_process(new QProcess(this))
{
    setWindowTitle(tr("Package Manager"));
    resize(900, 600);
    auto *layout = new QVBoxLayout(this);
    m_filterLineEdit = new QLineEdit(this);
    m_filterLineEdit->setPlaceholderText(tr("Filter packages..."));
    m_filterLineEdit->setMinimumHeight(32);
    layout->addWidget(m_filterLineEdit);

    m_tableWidget = new QTableWidget(this);
    m_tableWidget->setColumnCount(3);
    m_tableWidget->setHorizontalHeaderLabels({tr("Installed"), tr("Package"), tr("Description")});
    m_tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableWidget->setSortingEnabled(true);
    m_tableWidget->setItemDelegateForColumn(0, new CheckBoxDelegate(this));
    m_tableWidget->verticalHeader()->setVisible(false);
    layout->addWidget(m_tableWidget);

    auto *buttonLayout = new QHBoxLayout();
    m_refreshButton = new QPushButton(tr("Refresh"), this);
    m_installButton = new QPushButton(tr("Install"), this);
    m_uninstallButton = new QPushButton(tr("Uninstall"), this);
    buttonLayout->addWidget(m_refreshButton);
    buttonLayout->addWidget(m_installButton);
    buttonLayout->addWidget(m_uninstallButton);
    layout->addLayout(buttonLayout);

    m_logTextEdit = new QTextEdit(this);
    m_logTextEdit->setReadOnly(true);
    m_logTextEdit->setVisible(false);
    layout->addWidget(m_logTextEdit);

    connect(m_filterLineEdit, &QLineEdit::textChanged, this, &PackageManagerDialog::filterPackages);
    connect(m_refreshButton, &QPushButton::clicked, this, &PackageManagerDialog::refreshPackages);
    connect(m_installButton, &QPushButton::clicked, this, &PackageManagerDialog::installPackage);
    connect(m_uninstallButton, &QPushButton::clicked, this, &PackageManagerDialog::uninstallPackage);
    connect(
        m_process,
        &QProcess::readyReadStandardOutput,
        this,
        &PackageManagerDialog::processReadyRead);
    connect(
        m_process, &QProcess::readyReadStandardError, this, &PackageManagerDialog::processReadyRead);
    connect(
        m_process,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this,
        &PackageManagerDialog::processFinished);

    refreshPackages();
}

PackageManagerDialog::~PackageManagerDialog() {}

void PackageManagerDialog::refreshPackages()
{
    m_logTextEdit->clear();
    m_logTextEdit->setVisible(false);
    populateInstalledPackages();
    // First update packages
    QProcess updateProc;
    updateProc.start("r2pm", QStringList() << "-U");
    if (!updateProc.waitForFinished(30000)) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to run r2pm -U"));
        return;
    }
    // Then list packages
    QProcess listProc;
    listProc.start("r2pm", QStringList() << "-sj");
    if (!listProc.waitForFinished(30000)) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to run r2pm -sj"));
        return;
    }
    QByteArray out = listProc.readAllStandardOutput();
    QJsonDocument doc = QJsonDocument::fromJson(out);
    if (!doc.isArray()) {
        QMessageBox::warning(this, tr("Error"), tr("Invalid JSON output from r2pm -sj"));
        return;
    }
    QJsonArray array = doc.array();
    m_tableWidget->setRowCount(0);
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        QJsonObject obj = value.toObject();
        QString name = obj["name"].toString();
        QString desc = obj["description"].toString();
        if (desc == "") {
            desc = obj["desc"].toString();
        }
        int row = m_tableWidget->rowCount();
        m_tableWidget->insertRow(row);
        QTableWidgetItem *itemInstalled = new QTableWidgetItem();
        itemInstalled->setText(m_installedPackages.contains(name) ? "1" : "0");
        itemInstalled->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_tableWidget->setItem(row, 0, itemInstalled);
        QTableWidgetItem *itemName = new QTableWidgetItem(name);
        itemName->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_tableWidget->setItem(row, 1, itemName);
        QTableWidgetItem *itemDesc = new QTableWidgetItem(desc);
        itemDesc->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_tableWidget->setItem(row, 2, itemDesc);
    }
    filterPackages(m_filterLineEdit->text());
}

void PackageManagerDialog::populateInstalledPackages()
{
    m_installedPackages.clear();
    QProcess proc;
    proc.start("r2pm", QStringList() << "-l");
    if (!proc.waitForFinished(30000))
        return;
    QByteArray out = proc.readAllStandardOutput();
    QStringList lines = QString::fromLocal8Bit(out).split('\n', Qt::SkipEmptyParts);
    for (const QString &l : lines) {
        m_installedPackages.insert(l.trimmed());
    }
}

void PackageManagerDialog::filterPackages(const QString &text)
{
    for (int row = 0; row < m_tableWidget->rowCount(); ++row) {
        bool visible = text.isEmpty()
                       || m_tableWidget->item(row, 1)->text().contains(text, Qt::CaseInsensitive)
                       || m_tableWidget->item(row, 2)->text().contains(text, Qt::CaseInsensitive);
        m_tableWidget->setRowHidden(row, !visible);
    }
}

void PackageManagerDialog::installPackage()
{
    if (m_process->state() != QProcess::NotRunning)
        return;
    if (m_tableWidget->currentRow() < 0) {
        QMessageBox::information(this, tr("Install"), tr("Please select a package to install."));
        return;
    }
    QString pkg = m_tableWidget->item(m_tableWidget->currentRow(), 1)->text();
    m_logTextEdit->clear();
    m_logTextEdit->setVisible(true);
    m_process->start("r2pm", QStringList() << "-ci" << pkg);
}

void PackageManagerDialog::uninstallPackage()
{
    if (m_process->state() != QProcess::NotRunning)
        return;
    if (m_tableWidget->currentRow() < 0) {
        QMessageBox::information(this, tr("Uninstall"), tr("Please select a package to uninstall."));
        return;
    }
    QString pkg = m_tableWidget->item(m_tableWidget->currentRow(), 1)->text();
    m_logTextEdit->clear();
    m_logTextEdit->setVisible(true);
    m_process->start("r2pm", QStringList() << "-u" << pkg);
}

void PackageManagerDialog::processReadyRead()
{
    QByteArray out = m_process->readAllStandardOutput() + m_process->readAllStandardError();
    m_logTextEdit->append(QString::fromLocal8Bit(out));
}

void PackageManagerDialog::processFinished(int /*exitCode*/, QProcess::ExitStatus /*status*/)
{
    refreshPackages();
}
