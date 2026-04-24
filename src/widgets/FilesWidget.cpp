#include "widgets/FilesWidget.h"
#include "common/Helpers.h"
#include <QAbstractItemView>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVBoxLayout>

FilesWidget::FilesWidget(MainWindow *main)
    : IaitoDockWidget(main)
    , mainWindow(main)
{
    setWindowTitle(tr("Files"));
    setObjectName("FilesWidget");

    QWidget *container = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(container);

    filesModel = new QStandardItemModel(this);
    filesView = new QTableView(this);
    filesView->setModel(filesModel);
    qhelpers::bindFont(filesView, false, true);
    filesView->verticalHeader()->setVisible(false);
    filesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    filesView->setSelectionMode(QAbstractItemView::SingleSelection);
    filesView->horizontalHeader()->setStretchLastSection(true);
    mainLayout->addWidget(filesView);

    QHBoxLayout *rowLayout = new QHBoxLayout();
    uriCombo = new QComboBox(this);
    QSet<QString> seen;
    QStringList uris;
    for (const auto &desc : Core()->getRIOPluginDescriptions()) {
        for (const QString &uri : desc.uris) {
            if (!seen.contains(uri)) {
                seen.insert(uri);
                uris.append(uri);
            }
        }
    }
    uris.sort(Qt::CaseInsensitive);
    uriCombo->addItems(uris);
    rowLayout->addWidget(uriCombo);

    fileEdit = new QLineEdit(this);
    fileEdit->setPlaceholderText(tr("Filename"));
    qhelpers::attachFilePathCompleter(fileEdit);
    rowLayout->addWidget(fileEdit);

    parseCheck = new QCheckBox(tr("Parse Bin Info"), this);
    parseCheck->setChecked(true);
    rowLayout->addWidget(parseCheck);

    baseAddrEdit = new QLineEdit(this);
    baseAddrEdit->setPlaceholderText(tr("Base Address"));
    rowLayout->addWidget(baseAddrEdit);

    openButton = new QPushButton(tr("Open File"), this);
    connect(openButton, &QPushButton::clicked, this, &FilesWidget::onOpenButtonClicked);
    rowLayout->addWidget(openButton);

    closeButton = new QPushButton(tr("Close Selected File"), this);
    connect(closeButton, &QPushButton::clicked, this, &FilesWidget::onCloseButtonClicked);
    rowLayout->addWidget(closeButton);

    mainLayout->addLayout(rowLayout);
    setWidget(container);

    // Refresh listing whenever core triggers a global refresh (e.g., file load)
    connect(Core(), &IaitoCore::refreshAll, this, &FilesWidget::loadOpenedFiles);
    loadOpenedFiles();
}

void FilesWidget::loadOpenedFiles()
{
    filesModel->clear();
    filesModel->setColumnCount(4);
    filesModel->setHeaderData(0, Qt::Horizontal, tr("FD"), Qt::DisplayRole);
    filesModel->setHeaderData(1, Qt::Horizontal, tr("Perm"), Qt::DisplayRole);
    filesModel->setHeaderData(2, Qt::Horizontal, tr("Size"), Qt::DisplayRole);
    filesModel->setHeaderData(3, Qt::Horizontal, tr("Name"), Qt::DisplayRole);
    QJsonArray arr = Core()->getOpenedFiles();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr.at(i).toObject();
        int fd = obj["fd"].toInt();
        QString uri = obj["uri"].toString();
        bool writable = obj["writable"].toBool();
        bool raised = obj["raised"].toBool();
        qint64 size = static_cast<qint64>(obj["size"].toDouble());
        auto itemFd = new QStandardItem(QString::number(fd));
        auto itemPerm = new QStandardItem(writable ? QStringLiteral("rw-") : QStringLiteral("r--"));
        auto itemSize = new QStandardItem(QString::number(size));
        auto itemUri = new QStandardItem(uri);
        if (raised) {
            QFont f = itemFd->font();
            f.setBold(true);
            itemFd->setFont(f);
            itemPerm->setFont(f);
            itemSize->setFont(f);
            itemUri->setFont(f);
        }
        for (QStandardItem *it : {itemFd, itemPerm, itemSize, itemUri}) {
            it->setEditable(false);
        }
        filesModel->setItem(i, 0, itemFd);
        filesModel->setItem(i, 1, itemPerm);
        filesModel->setItem(i, 2, itemSize);
        filesModel->setItem(i, 3, itemUri);
    }
    filesView->resizeColumnsToContents();
}

void FilesWidget::onCloseButtonClicked()
{
    auto sel = filesView->selectionModel()->selectedRows();
    if (sel.isEmpty()) {
        return;
    }
    for (const QModelIndex &index : sel) {
        int fd = filesModel->item(index.row(), 0)->text().toInt();
        Core()->cmdRaw(QString("o-%1").arg(fd));
    }
    loadOpenedFiles();
}

void FilesWidget::onOpenButtonClicked()
{
    QString prefix = uriCombo->currentText();
    QString filename = fileEdit->text().trimmed();
    if (filename.isEmpty()) {
        return;
    }
    QString target = IaitoCore::sanitizeStringForCommand(prefix + filename);
    bool parse = parseCheck->isChecked();
    QString cmd;
    if (parse) {
        QString base = IaitoCore::sanitizeStringForCommand(baseAddrEdit->text().trimmed());
        if (!base.isEmpty()) {
            cmd = QString("o %1 %2").arg(target, base);
        } else {
            cmd = QString("o %1").arg(target);
        }
    } else {
        cmd = QString("on %1").arg(target);
    }
    Core()->cmdRaw(cmd);
    loadOpenedFiles();
}
