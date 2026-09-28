
#include "QuickFilterView.h"
#include "common/AddressableItemModel.h"
#include "ui_QuickFilterView.h"

#include <QSet>
#include <QSignalBlocker>
#include <QToolButton>

QuickFilterView::QuickFilterView(QWidget *parent, bool defaultOn)
    : QWidget(parent)
    , ui(new Ui::QuickFilterView())
{
    ui->setupUi(this);

    filterTimer.setSingleShot(true);
    filterTimer.setInterval(150);
    connect(&filterTimer, &QTimer::timeout, this, [this] {
        emit filterTextChanged(ui->filterLineEdit->text());
    });
    connect(ui->filterLineEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.isEmpty()) {
            filterTimer.stop();
            emit filterTextChanged(text);
        } else {
            filterTimer.start();
        }
    });
    connect(ui->typeFilterComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        const QVariant type = ui->typeFilterComboBox->currentData();
        if (typeModel) {
            typeModel->setTypeFilter(type);
        }
        emit filterTypeChanged(type);
    });

    if (!defaultOn) {
        closeFilter();
    }
}

QuickFilterView::~QuickFilterView() {}

void QuickFilterView::setTypeFilter(AddressableFilterProxyModel *model, int column)
{
    disconnect(sourceChangedConnection);
    disconnect(modelDestroyedConnection);
    if (typeModel && typeModel != model) {
        typeModel->setTypeFilter({});
        typeModel->setTypeFilterColumn(-1);
    }
    typeModel = model;
    typeColumn = column;
    if (model) {
        model->setTypeFilterColumn(column);
        sourceChangedConnection = connect(
            model, &QAbstractProxyModel::sourceModelChanged, this, &QuickFilterView::bindTypeSource);
        modelDestroyedConnection = connect(model, &QObject::destroyed, this, [this] {
            setTypeFilter(nullptr, -1);
        });
    }
    bindTypeSource();
    ui->typeFilterComboBox->setVisible(model && column >= 0 && !ui->filterLineEdit->isHidden());
}

void QuickFilterView::bindTypeSource()
{
    for (const auto &connection : typeConnections) {
        disconnect(connection);
    }
    typeConnections.clear();
    typeSource = typeModel ? typeModel->sourceModel() : nullptr;
    if (typeSource) {
        typeConnections << connect(
            typeSource, &QAbstractItemModel::modelReset, this, &QuickFilterView::updateTypes);
        typeConnections << connect(
            typeSource, &QAbstractItemModel::rowsInserted, this, &QuickFilterView::updateTypes);
        typeConnections << connect(
            typeSource, &QAbstractItemModel::rowsRemoved, this, &QuickFilterView::updateTypes);
        typeConnections << connect(
            typeSource, &QAbstractItemModel::layoutChanged, this, &QuickFilterView::updateTypes);
        typeConnections << connect(
            typeSource,
            &QAbstractItemModel::dataChanged,
            this,
            [this](const QModelIndex &first, const QModelIndex &last) {
                if (first.column() <= typeColumn && last.column() >= typeColumn) {
                    updateTypes();
                }
            });
        typeConnections << connect(
            typeSource, &QObject::destroyed, this, &QuickFilterView::updateTypes);
    }
    updateTypes();
}

void QuickFilterView::updateTypes()
{
    QSet<QString> values;
    if (typeSource && typeColumn >= 0) {
        const auto collect = [&](const auto &self, const QModelIndex &parent) -> void {
            for (int row = 0; row < typeSource->rowCount(parent); ++row) {
                values.insert(typeSource->index(row, typeColumn, parent).data().toString());
                const auto child = typeSource->index(row, 0, parent);
                if (typeSource->hasChildren(child)) {
                    self(self, child);
                }
            }
        };
        collect(collect, {});
    }
    QStringList types = values.values();
    types.sort();
    const QVariant selected = typeModel ? typeModel->typeFilter() : QVariant();
    const int selectedIndex = selected.isValid() ? types.indexOf(selected.toString()) + 1 : 0;
    {
        const QSignalBlocker blocker(ui->typeFilterComboBox);
        ui->typeFilterComboBox->clear();
        ui->typeFilterComboBox->addItem(tr("All types"), QVariant());
        for (const QString &type : types) {
            ui->typeFilterComboBox->addItem(type.isEmpty() ? tr("(No type)") : type, type);
        }
        ui->typeFilterComboBox->setCurrentIndex(selectedIndex);
        ui->typeFilterComboBox->setEnabled(!types.isEmpty());
    }
    if (selected.isValid() && !selectedIndex && typeModel) {
        typeModel->setTypeFilter({});
        emit filterTypeChanged({});
    }
}

void QuickFilterView::addActionButton(QAction *action)
{
    auto button = new QToolButton(this);
    button->setAutoRaise(true);
    button->setDefaultAction(action);
    ui->horizontalLayout->addWidget(button, 0, Qt::AlignRight);
    hasActions = true;
    show();
}

void QuickFilterView::showFilter()
{
    show();
    ui->filterLineEdit->show();
    ui->typeFilterComboBox->setVisible(typeModel && typeColumn >= 0);
    ui->filterLineEdit->setFocus();
}

void QuickFilterView::clearFilter()
{
    if (ui->filterLineEdit->text().isEmpty() && ui->typeFilterComboBox->currentIndex() <= 0) {
        closeFilter();
    } else {
        ui->filterLineEdit->setText("");
        ui->typeFilterComboBox->setCurrentIndex(0);
    }
}

void QuickFilterView::closeFilter()
{
    ui->filterLineEdit->setText("");
    ui->typeFilterComboBox->setCurrentIndex(0);
    ui->filterLineEdit->hide();
    ui->typeFilterComboBox->hide();
    if (!hasActions) {
        hide();
    }
    emit filterClosed();
}
