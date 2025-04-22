#include "widgets/BinariesWidget.h"
#include "common/Helpers.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileDialog>

BinariesWidget::BinariesWidget(MainWindow *main)
    : IaitoDockWidget(main), mainWindow(main) {
    setWindowTitle(tr("Binaries"));
    setObjectName("BinariesWidget");

    QWidget *container = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(container);

    binariesModel = new QStandardItemModel(this);
    binariesView = new QTableView(this);
    binariesView->setModel(binariesModel);
    binariesView->verticalHeader()->setVisible(false);
    binariesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    binariesView->setSelectionMode(QAbstractItemView::SingleSelection);
    binariesView->horizontalHeader()->setStretchLastSection(true);
    mainLayout->addWidget(binariesView);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    selectCurrentButton = new QPushButton(tr("Select Current Binary"), this);
    connect(selectCurrentButton, &QPushButton::clicked, this, &BinariesWidget::onSelectCurrentClicked);
    btnLayout->addWidget(selectCurrentButton);
    selectAllButton = new QPushButton(tr("Select All Binaries"), this);
    connect(selectAllButton, &QPushButton::clicked, this, &BinariesWidget::onSelectAllClicked);
    btnLayout->addWidget(selectAllButton);
    closeButton = new QPushButton(tr("Close Selected Binary"), this);
    connect(closeButton, &QPushButton::clicked, this, &BinariesWidget::onCloseButtonClicked);
    btnLayout->addWidget(closeButton);
    mainLayout->addLayout(btnLayout);

    QHBoxLayout *openLayout = new QHBoxLayout();
    fileEdit = new QLineEdit(this);
    fileEdit->setPlaceholderText(tr("Filename"));
    openLayout->addWidget(fileEdit);
    browseButton = new QPushButton(tr("Browse..."), this);
    connect(browseButton, &QPushButton::clicked, this, &BinariesWidget::onBrowseClicked);
    openLayout->addWidget(browseButton);
    openButton = new QPushButton(tr("Open Binary"), this);
    connect(openButton, &QPushButton::clicked, this, &BinariesWidget::onOpenButtonClicked);
    openLayout->addWidget(openButton);
    mainLayout->addLayout(openLayout);

    setWidget(container);
    connect(Core(), &IaitoCore::refreshAll, this, &BinariesWidget::loadBinaries);
    loadBinaries();
}

void BinariesWidget::loadBinaries() {
    binariesModel->clear();
    const QStringList headers = {tr("Name"), tr("IOFD"), tr("BFID"), tr("Size"), tr("Addr"), tr("Arch"), tr("Bits"), tr("BinOffset"), tr("ObjSize")};
    binariesModel->setColumnCount(headers.size());
    for (int i = 0; i < headers.size(); ++i) {
        binariesModel->setHeaderData(i, Qt::Horizontal, headers.at(i));
    }
    QJsonArray arr = Core()->cmdj("objj").array();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr.at(i).toObject();
        QString name = o["name"].toString();
        int iofd = o["iofd"].toInt();
        int bfid = o["bfid"].toInt();
        quint64 size = o["size"].toVariant().toULongLong();
        quint64 addr = o["addr"].toVariant().toULongLong();
        QJsonObject obj = o["obj"].toObject();
        QString arch = obj["arch"].toString();
        int bits = obj["bits"].toInt();
        quint64 binOffset = obj["binoffset"].toVariant().toULongLong();
        quint64 objSize = obj["objsize"].toVariant().toULongLong();
        QList<QStandardItem *> row;
        row << new QStandardItem(name);
        row << new QStandardItem(QString::number(iofd));
        row << new QStandardItem(QString::number(bfid));
        row << new QStandardItem(QString::number(size));
        row << new QStandardItem(RAddressString(addr));
        row << new QStandardItem(arch);
        row << new QStandardItem(QString::number(bits));
        row << new QStandardItem(QString::number(binOffset));
        row << new QStandardItem(QString::number(objSize));
        binariesModel->appendRow(row);
    }
    binariesView->resizeColumnsToContents();
}

void BinariesWidget::onSelectCurrentClicked() {
    auto sel = binariesView->selectionModel()->selectedRows();
    if (sel.isEmpty()) {
        return;
    }
    int bfid = binariesModel->item(sel.first().row(), 2)->text().toInt();
    Core()->cmdRaw(QString("ob %1").arg(bfid));
}

void BinariesWidget::onSelectAllClicked() {
    Core()->cmdRaw("ob *");
}

void BinariesWidget::onCloseButtonClicked() {
    auto sel = binariesView->selectionModel()->selectedRows();
    for (const QModelIndex &idx : sel) {
        int bfid = binariesModel->item(idx.row(), 2)->text().toInt();
        Core()->cmdRaw(QString("ob-%1").arg(bfid));
    }
    loadBinaries();
}

void BinariesWidget::onBrowseClicked() {
    QString fn = QFileDialog::getOpenFileName(this, tr("Open Binary"), QString(), tr("All Files (*)"));
    if (!fn.isEmpty()) {
        fileEdit->setText(fn);
    }
}

void BinariesWidget::onOpenButtonClicked() {
    QString fn = fileEdit->text().trimmed();
    if (fn.isEmpty()) {
        return;
    }
    Core()->cmdRaw(QString("obf \"%1\"").arg(fn));
    loadBinaries();
}