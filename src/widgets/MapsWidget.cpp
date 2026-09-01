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
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <QVariant>

#include <limits>

struct MapDialogValues
{
    int fd;
    int permissions;
    quint64 physicalAddress;
    quint64 virtualAddress;
    quint64 size;
    QString name;
};

// Helper to parse numbers from strings (supports decimal and hex with 0x prefix)
static bool parseNumber(const QString &text, quint64 &value)
{
    QString s = text.trimmed();
    if (s.startsWith('-')) {
        return false;
    }
    bool ok = false;
    if (s.startsWith("0x") || s.startsWith("0X")) {
        value = s.mid(2).toULongLong(&ok, 16);
    } else {
        value = s.toULongLong(&ok, 10);
        if (!ok) {
            value = s.toULongLong(&ok, 16);
        }
    }
    return ok;
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
        formLayout->addRow(tr("End Address (inclusive):"), endEdit);

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
        okButton = btns->button(QDialogButtonBox::Ok);
        validationLabel = new QLabel(this);
        validationLabel->setWordWrap(true);
        QPalette validationPalette = validationLabel->palette();
        validationPalette.setColor(QPalette::WindowText, Qt::red);
        validationLabel->setPalette(validationPalette);
        formLayout->addRow(validationLabel);
        connect(btns, &QDialogButtonBox::accepted, this, [this]() {
            MapDialogValues values;
            QString error;
            if (getValues(values, error)) {
                accept();
            }
        });
        connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
        formLayout->addRow(btns);

        if (!init.isEmpty()) {
            int initFd = init["fd"].toInt();
            qhelpers::selectIndexByData(fdCombo, initFd);
            nameEdit->setText(init["name"].toString());
            permEdit->setText(init["perm"].toString());
            physEdit->setText(init["physical"].toString());
            virtEdit->setText(init["virtual"].toString());
            sizeEdit->setText(init["size"].toString());
            endEdit->setText(init["end"].toString());
        }

        connect(fdCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            updateValidation();
        });
        connect(permEdit, &QLineEdit::textChanged, this, [this]() { updateValidation(); });
        connect(physEdit, &QLineEdit::textChanged, this, [this]() { updateValidation(); });
        connect(virtEdit, &QLineEdit::textChanged, this, [this]() { updateValidation(); });
        connect(sizeEdit, &QLineEdit::textChanged, this, [this]() { updateValidation(); });
        connect(endEdit, &QLineEdit::textChanged, this, [this]() { updateValidation(); });
        connect(useEndCheck, &QCheckBox::toggled, this, [this]() { updateValidation(); });
        updateValidation();
    }

    bool getValues(MapDialogValues &values, QString &error) const
    {
        if (fdCombo->currentIndex() < 0) {
            error = tr("Select an underlying file descriptor.");
            return false;
        }
        values.fd = fdCombo->currentData().toInt();
        if (!parseNumber(physEdit->text(), values.physicalAddress)) {
            error = tr("Physical address is not a valid number.");
            return false;
        }
        if (!parseNumber(virtEdit->text(), values.virtualAddress)) {
            error = tr("Virtual address is not a valid number.");
            return false;
        }
        if (useEndCheck->isChecked()) {
            quint64 end = 0;
            if (!parseNumber(endEdit->text(), end)) {
                error = tr("End address is not a valid number.");
                return false;
            }
            if (end < values.virtualAddress) {
                error = tr("End address must not be below the virtual address.");
                return false;
            }
            quint64 distance = end - values.virtualAddress;
            if (distance == std::numeric_limits<quint64>::max()) {
                error = tr("The selected address range is too large.");
                return false;
            }
            values.size = distance + 1;
        } else if (!parseNumber(sizeEdit->text(), values.size) || values.size == 0) {
            error = tr("Size must be a valid number greater than zero.");
            return false;
        }
        quint64 lastOffset = values.size - 1;
        quint64 maxAddress = std::numeric_limits<quint64>::max();
        if (lastOffset > maxAddress - values.virtualAddress
            || lastOffset > maxAddress - values.physicalAddress) {
            error = tr("The map range exceeds the address space.");
            return false;
        }
        QByteArray perm = permEdit->text().trimmed().toUtf8();
        int prefixLength = perm.startsWith('s') ? 1 : 0;
        if (perm.size() != prefixLength + 3
            || (perm[prefixLength] != 'r' && perm[prefixLength] != '-')
            || (perm[prefixLength + 1] != 'w' && perm[prefixLength + 1] != '-')
            || (perm[prefixLength + 2] != 'x' && perm[prefixLength + 2] != '-')) {
            error = tr("Permissions must be a valid rwx string, such as r-x or rw-.");
            return false;
        }
        values.permissions = r_str_rwx(perm.constData());
        if (values.permissions < 0) {
            error = tr("Permissions must be a valid rwx string, such as r-x or rw-.");
            return false;
        }
        values.name = nameEdit->text();
        return true;
    }

private:
    void updateValidation()
    {
        MapDialogValues values;
        QString error;
        bool valid = getValues(values, error);
        okButton->setEnabled(valid);
        validationLabel->setText(error);
        validationLabel->setVisible(!valid);
    }

    QComboBox *fdCombo;
    QLineEdit *nameEdit;
    QLineEdit *permEdit;
    QLineEdit *physEdit;
    QLineEdit *virtEdit;
    QCheckBox *useEndCheck;
    QLineEdit *sizeEdit;
    QLineEdit *endEdit;
    QPushButton *okButton;
    QLabel *validationLabel;
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
    connect(mapsView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        updateMapActions();
    });

    updateMapActions();
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
        bankCombo->addItem(row, id);
    }
    bankCombo->blockSignals(false);
    if (bankCombo->count() > 0) {
        bankCombo->setCurrentIndex(0);
        onBankChanged(0);
    }
}

void MapsWidget::onBankChanged(int idx)
{
    bool ok = false;
    int bankId = bankCombo->itemData(idx).toInt(&ok);
    if (!ok) {
        return;
    }
    Core()->cmd(QString("omb %1").arg(bankId));
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
    name = Core()->sanitizeStringForCommand(name).replace('\n', '_').replace('\r', '_');
    Core()->cmdRaw(QString("omb+ %1").arg(name));
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
    updateMapActions();
}

void MapsWidget::updateMapActions()
{
    int selectionCount = mapsView->selectionModel()->selectedRows().size();
    bool hasSelection = selectionCount > 0;
    delMapBtn->setEnabled(hasSelection);
    editMapBtn->setEnabled(selectionCount == 1);
    priorMapBtn->setEnabled(hasSelection);
    depriorMapBtn->setEnabled(hasSelection);
}

void MapsWidget::onAddMap()
{
    MapDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    MapDialogValues values;
    QString error;
    if (!dlg.getValues(values, error)) {
        return;
    }
    {
        RCoreLocked core = Core()->core();
        if (!r_io_desc_get(core->io, values.fd)) {
            error = tr("The selected file descriptor is no longer available.");
        } else {
            RIOMap *map = r_io_map_add(
                core->io,
                values.fd,
                values.permissions,
                values.physicalAddress,
                values.virtualAddress,
                values.size);
            if (!map) {
                error = tr("radare2 could not create the map with these values.");
            } else {
                QByteArray name = values.name.toUtf8();
                r_io_map_set_name(map, name.constData());
                r_core_block_read(core);
            }
        }
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Add Map"), error);
        return;
    }
    refreshMaps();
}

void MapsWidget::onDeleteMap()
{
    auto sel = mapsView->selectionModel()->selectedRows();
    if (sel.isEmpty()) {
        return;
    }
    if (sel.size() > 1
        && QMessageBox::question(
               this,
               tr("Delete Maps"),
               tr("Delete %1 selected maps?").arg(sel.size()),
               QMessageBox::Yes | QMessageBox::No,
               QMessageBox::No)
               != QMessageBox::Yes) {
        return;
    }
    for (const QModelIndex &idx : sel) {
        int id = mapsModel->item(idx.row(), 0)->text().toInt();
        Core()->cmd(QString("om- %1").arg(id));
    }
    refreshMaps();
}

void MapsWidget::onEditMap()
{
    auto sel = mapsView->selectionModel()->selectedRows();
    if (sel.size() != 1) {
        return;
    }
    ut32 id = mapsModel->item(sel.first().row(), 0)->text().toUInt();
    QJsonObject init;
    bool mapFound = false;
    {
        RCoreLocked core = Core()->core();
        RIOMap *map = r_io_map_get(core->io, id);
        if (map) {
            quint64 virtualAddress = r_io_map_begin(map);
            quint64 size = r_io_map_size(map);
            init["fd"] = map->fd;
            init["name"] = QString::fromUtf8(map->name ? map->name : "");
            init["perm"] = QString::fromUtf8(r_str_rwx_i(map->perm));
            init["physical"] = RAddressString(map->delta);
            init["virtual"] = RAddressString(virtualAddress);
            init["size"] = RSizeString(size);
            init["end"] = RAddressString(virtualAddress + size - 1);
            mapFound = true;
        }
    }
    if (!mapFound) {
        QMessageBox::warning(this, tr("Edit Map"), tr("The selected map no longer exists."));
        return;
    }
    MapDialog dlg(this, init);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    MapDialogValues values;
    QString error;
    if (!dlg.getValues(values, error)) {
        return;
    }
    {
        RCoreLocked core = Core()->core();
        RIOMap *map = r_io_map_get(core->io, id);
        RIODesc *desc = r_io_desc_get(core->io, values.fd);
        if (!map) {
            error = tr("The selected map no longer exists.");
        } else if (!desc) {
            error = tr("The selected file descriptor is no longer available.");
        } else {
            quint64 oldVirtualAddress = r_io_map_begin(map);
            if (!r_io_map_remap(core->io, id, values.virtualAddress)) {
                error = tr("radare2 could not change the map's virtual address.");
            } else if (!r_io_map_resize(core->io, id, values.size)) {
                r_io_map_remap(core->io, id, oldVirtualAddress);
                error = tr("radare2 could not change the map's size.");
            } else {
                map->fd = values.fd;
                map->delta = values.physicalAddress;
                map->perm = values.permissions;
                QByteArray name = values.name.toUtf8();
                r_io_map_set_name(map, name.constData());
                r_core_block_read(core);
            }
        }
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Edit Map"), error);
        return;
    }
    refreshMaps();
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
