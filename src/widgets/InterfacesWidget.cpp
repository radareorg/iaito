#include "InterfacesWidget.h"
#include "common/Helpers.h"
#include "core/MainWindow.h"
#include "ui_ListDockWidget.h"

#include <QRegularExpression>

namespace {

QStringList classdumpLanguages()
{
    QStringList languages;
    const QString output = Core()->cmdRaw("iccl").trimmed();
    if (!output.contains(QStringLiteral("invalid"), Qt::CaseInsensitive)
        && !output.contains(QStringLiteral("unknown"), Qt::CaseInsensitive)
        && !output.contains(QStringLiteral("0x"), Qt::CaseInsensitive)
        && !output.contains(QLatin1Char('{')) && !output.contains(QLatin1Char(';'))) {
        static const QRegularExpression languagePattern(QStringLiteral("^[A-Za-z0-9_+.-]+$"));
        for (const QString &line :
             output.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
            const QString language = line.trimmed();
            if (languagePattern.match(language).hasMatch() && !language.startsWith(QLatin1Char('-'))
                && !languages.contains(language)) {
                languages << language;
            }
        }
    }
    if (languages.isEmpty()) {
        languages << QStringLiteral("cxx") << QStringLiteral("java") << QStringLiteral("swift");
    }
    return languages;
}

QList<InterfaceEntry> parseClassdump(const QString &language, const QString &classdump)
{
    QList<InterfaceEntry> entries;
    static const QRegularExpression addressPattern(QStringLiteral("0x[0-9a-fA-F]+"));

    const QStringList lines = classdump.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        QRegularExpressionMatchIterator matches = addressPattern.globalMatch(line);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            bool ok = false;
            const RVA address = match.captured(0).mid(2).toULongLong(&ok, 16);
            if (!ok || address == 0) {
                continue;
            }
            entries << InterfaceEntry{language, address, line.trimmed()};
            break;
        }
    }
    return entries;
}

} // namespace

InterfacesModel::InterfacesModel(QList<InterfaceEntry> *entries, QObject *parent)
    : AddressableItemModel<QAbstractListModel>(parent)
    , entries(entries)
{}

void InterfacesModel::setEntries(const QList<InterfaceEntry> &entries)
{
    beginResetModel();
    *this->entries = entries;
    endResetModel();
}

int InterfacesModel::rowCount(const QModelIndex &) const
{
    return entries->count();
}

int InterfacesModel::columnCount(const QModelIndex &) const
{
    return InterfacesModel::ColumnCount;
}

QVariant InterfacesModel::data(const QModelIndex &index, int role) const
{
    if (index.row() >= entries->count()) {
        return QVariant();
    }

    const InterfaceEntry &entry = entries->at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case AddressColumn:
            return RAddressString(entry.address);
        case LanguageColumn:
            return entry.language;
        case TextColumn:
            return entry.text;
        default:
            return QVariant();
        }
    case InterfaceEntryRole:
        return QVariant::fromValue(entry);
    default:
        return QVariant();
    }
}

QVariant InterfacesModel::headerData(int section, Qt::Orientation, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        switch (section) {
        case AddressColumn:
            return tr("Address");
        case LanguageColumn:
            return tr("Language");
        case TextColumn:
            return tr("Interface");
        default:
            return QVariant();
        }
    default:
        return QVariant();
    }
}

RVA InterfacesModel::address(const QModelIndex &index) const
{
    return entries->at(index.row()).address;
}

QString InterfacesModel::name(const QModelIndex &index) const
{
    return entries->at(index.row()).text;
}

InterfacesProxyModel::InterfacesProxyModel(InterfacesModel *sourceModel, QObject *parent)
    : AddressableFilterProxyModel(sourceModel, parent)
{}

bool InterfacesProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    QModelIndex index = sourceModel()->index(row, 0, parent);
    InterfaceEntry entry = index.data(InterfacesModel::InterfaceEntryRole).value<InterfaceEntry>();
    return entry.language.contains(FILTER_REGEX) || entry.text.contains(FILTER_REGEX)
           || RAddressString(entry.address).contains(FILTER_REGEX);
}

bool InterfacesProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    InterfaceEntry leftEntry
        = left.data(InterfacesModel::InterfaceEntryRole).value<InterfaceEntry>();
    InterfaceEntry rightEntry
        = right.data(InterfacesModel::InterfaceEntryRole).value<InterfaceEntry>();

    switch (left.column()) {
    case InterfacesModel::AddressColumn:
        return leftEntry.address < rightEntry.address;
    case InterfacesModel::LanguageColumn:
        return leftEntry.language < rightEntry.language;
    case InterfacesModel::TextColumn:
        return leftEntry.text < rightEntry.text;
    default:
        return leftEntry.address < rightEntry.address;
    }
}

InterfacesWidget::InterfacesWidget(MainWindow *main)
    : ListDockWidget(main, ListDockWidget::SearchBarPolicy::HideByDefault)
{
    setWindowTitle(tr("Interfaces"));
    setObjectName("InterfacesWidget");

    interfacesModel = new InterfacesModel(&entries, this);
    interfacesProxyModel = new InterfacesProxyModel(interfacesModel, this);
    setModels(interfacesProxyModel);
    ui->treeView->sortByColumn(InterfacesModel::AddressColumn, Qt::AscendingOrder);
    showCount(true);

    connect(Core(), &IaitoCore::codeRebased, this, &InterfacesWidget::refreshInterfaces);
    connect(Core(), &IaitoCore::refreshAll, this, &InterfacesWidget::refreshInterfaces);

    refreshInterfaces();
}

InterfacesWidget::~InterfacesWidget()
{
    delete interfacesProxyModel;
    delete interfacesModel;
}

void InterfacesWidget::refreshInterfaces()
{
    QList<InterfaceEntry> newEntries;
    for (const QString &language : classdumpLanguages()) {
        newEntries.append(
            parseClassdump(language, Core()->cmd(QStringLiteral("icc %1").arg(language))));
    }
    interfacesModel->setEntries(newEntries);

    ui->treeView->resizeColumnToContents(0);
    ui->treeView->resizeColumnToContents(1);
}
