#include "widgets/MapsWidget.h"
#include "common/Helpers.h"
#include "core/MainWindow.h"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QStringList>
#include <QVBoxLayout>
#include <QVariant>

// Helper to parse numbers from strings (supports decimal and hex with 0x prefix)
static quint64 parseNumber(const QString &text)
{
    QString s = text.trimmed();
    bool ok = false;
    quint64 val = 0;
    if (s.startsWith("0x") || s.startsWith("0X")) {
        val = s.mid(2).toULongLong(&ok, 16);
    } else {
        val = s.toULongLong(&ok, 10);
        if (!ok) {
            val = s.toULongLong(&ok, 16);
        }
    }
    return ok ? val : 0;
}

// Dialog for adding/editing maps
class MapDialog : public QDialog
{
public:
    explicit MapDialog(QWidget *parent = nullptr, const QJsonObject &init = QJsonObject())
        : QDialog(parent)
    {
        setWindowTitle(init.isEmpty() ? tr("Add Map") : tr("Edit Map"));
        QFormLayout *formLayout = new QFormLayout(this);
        fdCombo = new QComboBox(this);
        QJsonArray fa = Core()->cmdj("oj").array();
        for (auto v : fa) {
            QJsonObject o = v.toObject();
            int fd = o["fd"].toInt();
            QString uri = o["uri"].toString();
            fdCombo->addItem(QString("%1: %2").arg(fd).arg(uri), fd);
        }
        nameEdit = new QLineEdit(this);
        permEdit = new QLineEdit(this);
        physEdit = new QLineEdit(this);
        virtEdit = new QLineEdit(this);
        useEndCheck = new QCheckBox(tr("Specify end address instead of size"), this);
        sizeEdit = new QLineEdit(this);
        endEdit = new QLineEdit(this);

        formLayout->addRow(tr("Underlying FD:"), fdCombo);
        formLayout->addRow(tr("Map Name:"), nameEdit);
        formLayout->addRow(tr("Permissions:"), permEdit);
        formLayout->addRow(tr("Physical Address:"), physEdit);
        formLayout->addRow(tr("Virtual Address:"), virtEdit);
        formLayout->addRow(useEndCheck);
        formLayout->addRow(tr("Size:"), sizeEdit);
        formLayout->addRow(tr("End Address:"), endEdit);

        useEndCheck->setChecked(false);
        endEdit->setEnabled(false);
        if (auto lbEnd = formLayout->labelForField(endEdit)) {
            lbEnd->setEnabled(false);
        }
        connect(useEndCheck, &QCheckBox::toggled, this, [formLayout, this](bool checked) {
            sizeEdit->setEnabled(!checked);
            if (auto lbSize = formLayout->labelForField(sizeEdit)) {
                lbSize->setEnabled(!checked);
            }
            endEdit->setEnabled(checked);
            if (auto lbEnd2 = formLayout->labelForField(endEdit)) {
                lbEnd2->setEnabled(checked);
            }
        });

        QDialogButtonBox *btns
            = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
        QVBoxLayout *vbox = new QVBoxLayout();
        vbox->addLayout(formLayout);
        formLayout->addRow(btns); // vbox->addWidget(btns);
        setLayout(vbox);

        if (!init.isEmpty()) {
            int initFd = init["fd"].toInt();
            qhelpers::selectIndexByData(fdCombo, initFd);
            nameEdit->setText(init["name"].toString());
            permEdit->setText(init["perm"].toString());
            QString fromStr = init["from"].toString();
            physEdit->setText(fromStr);
            QString toStr = init["to"].toString();
            endEdit->setText(toStr);
            quint64 sizeVal = parseNumber(toStr) - parseNumber(fromStr);
            QString sizeStr;
            if (toStr.startsWith("0x") || toStr.startsWith("0X")) {
                sizeStr = QString("0x%1").arg(sizeVal, 0, 16);
            } else {
                sizeStr = QString::number(sizeVal);
            }
            sizeEdit->setText(sizeStr);
            if (init.contains("virt")) {
                virtEdit->setText(init["virt"].toString());
            }
        }
    }

    QString getFrom() const { return physEdit->text(); }
    QString getSize() const
    {
        if (useEndCheck->isChecked()) {
            quint64 diff = parseNumber(endEdit->text()) - parseNumber(physEdit->text());
            if (endEdit->text().startsWith("0x") || endEdit->text().startsWith("0X")) {
                return QString("0x%1").arg(diff, 0, 16);
            }
            return QString::number(diff);
        }
        return sizeEdit->text();
    }
    QString getPerm() const { return permEdit->text(); }
    QString getFd() const { return QString::number(fdCombo->currentData().toInt()); }
    QString getMapName() const { return nameEdit->text(); }
    QString getVirt() const { return virtEdit->text(); }

private:
    QComboBox *fdCombo;
    QLineEdit *nameEdit;
    QLineEdit *permEdit;
    QLineEdit *physEdit;
    QLineEdit *virtEdit;
    QCheckBox *useEndCheck;
    QLineEdit *sizeEdit;
    QLineEdit *endEdit;
};

MapsWidget::MapsWidget(MainWindow *main)
    : IaitoDockWidget(main)
    , mainWindow(main)
{
    setWindowTitle(tr("Maps"));
    setObjectName("MapsWidget");
    QWidget *container = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(container);

    // Bank selector
    QHBoxLayout *bankLayout = new QHBoxLayout();
    bankCombo = new QComboBox(this);
    addBankBtn = new QPushButton(tr("Add Bank"), this);
    delBankBtn = new QPushButton(tr("Delete Current Bank"), this);
    bankLayout->addWidget(bankCombo);
    bankLayout->addWidget(addBankBtn);
    bankLayout->addWidget(delBankBtn);
    mainLayout->addLayout(bankLayout);

    // Maps table
    mapsModel = new QStandardItemModel(this);
    mapsView = new QTableView(this);
    mapsView->setModel(mapsModel);
    // Hide row header indices
    mapsView->verticalHeader()->setVisible(false);
    mapsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    mapsView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mainLayout->addWidget(mapsView);

    // Map controls
    QHBoxLayout *mapBtnLayout = new QHBoxLayout();
    addMapBtn = new QPushButton(tr("Add"), this);
    delMapBtn = new QPushButton(tr("Delete"), this);
    editMapBtn = new QPushButton(tr("Edit"), this);
    priorMapBtn = new QPushButton(tr("Up"), this);
    depriorMapBtn = new QPushButton(tr("Down"), this);
    mapBtnLayout->addWidget(addMapBtn);
    mapBtnLayout->addWidget(delMapBtn);
    mapBtnLayout->addWidget(editMapBtn);
    mapBtnLayout->addWidget(priorMapBtn);
    mapBtnLayout->addWidget(depriorMapBtn);
    mainLayout->addLayout(mapBtnLayout);

    setWidget(container);

    // Connections
    connect(addBankBtn, &QPushButton::clicked, this, &MapsWidget::onAddBank);
    connect(delBankBtn, &QPushButton::clicked, this, &MapsWidget::onDeleteBank);
    connect(
        bankCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &MapsWidget::onBankChanged);
    connect(addMapBtn, &QPushButton::clicked, this, &MapsWidget::onAddMap);
    connect(delMapBtn, &QPushButton::clicked, this, &MapsWidget::onDeleteMap);
    connect(editMapBtn, &QPushButton::clicked, this, &MapsWidget::onEditMap);
    connect(priorMapBtn, &QPushButton::clicked, this, &MapsWidget::onPrioritizeMap);
    connect(depriorMapBtn, &QPushButton::clicked, this, &MapsWidget::onDeprioritizeMap);

    loadBanks();
    // Refresh banks and maps when the core triggers a refresh (e.g., after binary load)
    refreshDeferrer = createRefreshDeferrer([this]() { loadBanks(); });
    connect(Core(), &IaitoCore::refreshAll, this, &MapsWidget::loadBanks);
}

MapsWidget::~MapsWidget() = default;

void MapsWidget::loadBanks()
{
    bankCombo->blockSignals(true);
    bankCombo->clear();
    QJsonArray ba = Core()->cmdj("ombj").array();
    for (auto v : ba) {
        QJsonObject o = v.toObject();
        auto id = o["id"].toInt();
        auto name = o["name"].toString();
        auto row = QStringLiteral("%1 %2").arg(id).arg(name);
        bankCombo->addItem(row);
    }
    bankCombo->blockSignals(false);
    if (bankCombo->count() > 0) {
        bankCombo->setCurrentIndex(0);
        onBankChanged(0);
    }
}

void MapsWidget::onBankChanged(int idx)
{
    QString bank = bankCombo->itemText(idx);
    Core()->cmd(QString("omb %1").arg(bank));
    refreshMaps();
}

void MapsWidget::onAddBank()
{
    bool ok = false;
    QString name = QInputDialog::getText(
        this, tr("Add Bank"), tr("Bank name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty()) {
        return;
    }
    Core()->cmd(QString("omb+ %1").arg(name));
    loadBanks();
}

void MapsWidget::onDeleteBank()
{
    QString bank = bankCombo->currentText();
    if (bank.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete Bank"), tr("Delete bank \"%1\"?").arg(bank))
        != QMessageBox::Yes) {
        return;
    }
    int id = atoi(bank.toStdString().c_str());
    Core()->cmd(QString("omb-%1").arg(id));
    loadBanks();
}

void MapsWidget::refreshMaps()
{
    mapsModel->clear();
    QStringList hdr = {tr("Map"), tr("FD"), tr("From"), tr("To"), tr("Perm"), tr("Name")};
    mapsModel->setHorizontalHeaderLabels(hdr);
    QJsonArray ma = Core()->cmdj("omj").array();
    for (auto v : ma) {
        QJsonObject o = v.toObject();
        QList<QStandardItem *> row;
        row << new QStandardItem(QString::number(o["map"].toInt()));
        row << new QStandardItem(QString::number(o["fd"].toInt()));
        row << new QStandardItem(RAddressString(o["from"].toVariant().toULongLong()));
        row << new QStandardItem(RAddressString(o["to"].toVariant().toULongLong()));
        row << new QStandardItem(o["perm"].toString());
        row << new QStandardItem(o["name"].toString());
        mapsModel->appendRow(row);
    }
    mapsView->resizeColumnsToContents();
}

void MapsWidget::onAddMap()
{
    {
        MapDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        QStringList parts;
        parts << dlg.getFd() << dlg.getVirt() << dlg.getSize() << dlg.getFrom() << dlg.getPerm()
              << dlg.getMapName();
        QString virt = dlg.getVirt();
        if (!virt.isEmpty()) {
            parts << virt;
        }
        QString cmd = QString("'om %1").arg(parts.join(" "));
        Core()->cmd(cmd);
        refreshMaps();
    }
}

void MapsWidget::onDeleteMap()
{
    auto sel = mapsView->selectionModel()->selectedRows();
    for (const QModelIndex &idx : sel) {
        int id = mapsModel->item(idx.row(), 0)->text().toInt();
        Core()->cmd(QString("om- %1").arg(id));
    }
    refreshMaps();
}

void MapsWidget::onEditMap()
{
    auto sel = mapsView->selectionModel()->selectedRows();
    if (sel.isEmpty())
        return;
    // int id = mapsModel->item(sel.first().row(), 0)->text().toInt();
    {
        QJsonObject init;
        int row = sel.first().row();
        init["fd"] = mapsModel->item(row, 1)->text().toInt();
        init["from"] = mapsModel->item(row, 2)->text();
        init["to"] = mapsModel->item(row, 3)->text();
        init["perm"] = mapsModel->item(row, 4)->text();
        init["name"] = mapsModel->item(row, 5)->text();
        MapDialog dlg(this, init);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        QStringList parts;
        parts << dlg.getFrom() << dlg.getSize() << dlg.getPerm() << dlg.getFd() << dlg.getMapName();
        QString virt = dlg.getVirt();
        if (!virt.isEmpty()) {
            parts << virt;
        }
        R_LOG_TODO("Not implemented");
#if 0
        QString cmd = QString("om= %1 %2").arg(id).arg(parts.join(" "));
        Core()->cmd(cmd);
#endif
        refreshMaps();
    }
}

void MapsWidget::onPrioritizeMap()
{
    auto sel = mapsView->selectionModel()->selectedRows();
    for (const QModelIndex &idx : sel) {
        int id = mapsModel->item(idx.row(), 0)->text().toInt();
        Core()->cmd(QString("omr %1").arg(id));
    }
    refreshMaps();
}

void MapsWidget::onDeprioritizeMap()
{
    auto sel = mapsView->selectionModel()->selectedRows();
    for (const QModelIndex &idx : sel) {
        int id = mapsModel->item(idx.row(), 0)->text().toInt();
        Core()->cmd(QString("omrd %1").arg(id));
    }
    refreshMaps();
}
