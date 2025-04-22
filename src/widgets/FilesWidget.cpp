#include "widgets/FilesWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

FilesWidget::FilesWidget(MainWindow *main)
    : IaitoDockWidget(main), mainWindow(main) {
    setWindowTitle(tr("Files"));
    setObjectName("FilesWidget");

    QWidget *container = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(container);

    filesModel = new QStandardItemModel(this);
    filesView = new QTableView(this);
    filesView->setModel(filesModel);
    // Hide row header indices
    filesView->verticalHeader()->setVisible(false);
    filesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    filesView->setSelectionMode(QAbstractItemView::SingleSelection);
    filesView->horizontalHeader()->setStretchLastSection(true);
    mainLayout->addWidget(filesView);

    closeButton = new QPushButton(tr("Close Selected File"), this);
    connect(closeButton, &QPushButton::clicked, this, &FilesWidget::onCloseButtonClicked);
    mainLayout->addWidget(closeButton);

    QHBoxLayout *rowLayout = new QHBoxLayout();
    uriCombo = new QComboBox(this);
    QSet<QString> seen;
    for (const auto &desc : Core()->getRIOPluginDescriptions()) {
        for (const QString &uri : desc.uris) {
            if (!seen.contains(uri)) {
                seen.insert(uri);
                uriCombo->addItem(uri);
            }
        }
    }
    rowLayout->addWidget(uriCombo);

    fileEdit = new QLineEdit(this);
    fileEdit->setPlaceholderText(tr("Filename"));
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

    mainLayout->addLayout(rowLayout);
    setWidget(container);

    // Refresh listing whenever core triggers a global refresh (e.g., file load)
    connect(Core(), &IaitoCore::refreshAll, this, &FilesWidget::loadOpenedFiles);
    loadOpenedFiles();
}

void FilesWidget::loadOpenedFiles() {
    filesModel->clear();
    filesModel->setColumnCount(2);
    filesModel->setHeaderData(0, Qt::Horizontal, tr("FD"));
    filesModel->setHeaderData(1, Qt::Horizontal, tr("URI"));
    QJsonArray arr = Core()->getOpenedFiles();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr.at(i).toObject();
        int fd = obj["fd"].toInt();
        QString uri = obj["uri"].toString();
        auto itemFd = new QStandardItem(QString::number(fd));
        auto itemUri = new QStandardItem(uri);
        filesModel->setItem(i, 0, itemFd);
        filesModel->setItem(i, 1, itemUri);
    }
    filesView->resizeColumnsToContents();
}

void FilesWidget::onCloseButtonClicked() {
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

void FilesWidget::onOpenButtonClicked() {
    QString prefix = uriCombo->currentText();
    QString filename = fileEdit->text().trimmed();
    if (filename.isEmpty()) {
        return;
    }
    QString target = prefix + filename;
    bool parse = parseCheck->isChecked();
    QString cmd;
    if (parse) {
        QString base = baseAddrEdit->text().trimmed();
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
