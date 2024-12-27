#include "SearchWidget.h"
#include "common/Helpers.h"
#include "core/MainWindow.h"
#include "ui_SearchWidget.h"

#include <QComboBox>
#include <QDockWidget>
#include <QShortcut>
#include <QTreeWidget>

namespace {

static const int kMaxTooltipWidth = 500;
static const int kMaxTooltipDisasmPreviewLines = 10;
static const int kMaxTooltipHexdumpBytes = 64;

} // namespace

static const QMap<QString, QString> searchBoundaries{
    {"io.maps", "All maps"},
    {"io.maps.x", "All executable maps"},
    {"io.maps.w", "All writeable maps"},
    {"io.map", "Current map"},
    {"raw", "Raw"},
    {"block", "Current block"},
    {"bin.section", "Current section"},
    {"bin.sections.x", "All executable sections"},
    {"bin.sections.w", "All writeable sections"},
    {"bin.sections", "All sections"},
    {"anal.function", "Current function"},
    {"anal.bb", "Current basic block"},
};

static const QMap<QString, QString> searchBoundariesDebug{
    {"dbg.maps", "All memory maps"},
    {"dbg.maps.x", "All executable maps"},
    {"dbg.maps.w", "All writeable maps"},
    {"dbg.map", "Memory map"},
    {"raw", "Raw"},
    {"block", "Current block"},
    {"dbg.stack", "Stack"},
    {"dbg.heap", "Heap"},
    {"anal.function", "Current function"},
    {"anal.bb", "Current basic block"},
};

SearchModel::SearchModel(QList<SearchDescription> *search, QObject *parent)
    : AddressableItemModel<QAbstractListModel>(parent)
    , search(search)
{}

int SearchModel::rowCount(const QModelIndex &) const
{
    return search->count();
}

int SearchModel::columnCount(const QModelIndex &) const
{
    return Columns::COUNT;
}

QVariant SearchModel::data(const QModelIndex &index, int role) const
{
    if (index.row() >= search->count())
        return QVariant();

    const SearchDescription &exp = search->at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case OFFSET:
            return RAddressString(exp.offset);
        case SIZE:
            return RSizeString(exp.size);
        case CODE:
            return exp.code;
        case DATA:
            return exp.data;
        case COMMENT:
            return Core()->getCommentAt(exp.offset);
        default:
            return QVariant();
        }
    case Qt::ToolTipRole: {
        QString previewContent = QString();
        // if result is CODE, show disassembly
        if (!exp.code.isEmpty()) {
            previewContent = Core()
                                 ->getDisassemblyPreview(exp.offset, kMaxTooltipDisasmPreviewLines)
                                 .join("<br>");
            // if result is DATA or Disassembly is N/A
        } else if (!exp.data.isEmpty() || previewContent.isEmpty()) {
            previewContent = Core()->getHexdumpPreview(exp.offset, kMaxTooltipHexdumpBytes);
        }

        const QFont &fnt = Config()->getBaseFont();
        QFontMetrics fm{fnt};

        QString toolTipContent
            = QStringLiteral("<html><div style=\"font-family: %1; font-size: %2pt; "
                      "white-space: nowrap;\">")
                  .arg(fnt.family())
                  .arg(qMax(6, fnt.pointSize() - 1)); // slightly decrease font size, to keep
                                                      // more text in the same box

        toolTipContent += tr("<div style=\"margin-bottom: "
                             "10px;\"><strong>Preview</strong>:<br>%1</div>")
                              .arg(previewContent);

        toolTipContent += "</div></html>";
        return toolTipContent;
    }
    case SearchDescriptionRole:
        return QVariant::fromValue(exp);
    default:
        return QVariant();
    }
}

QVariant SearchModel::headerData(int section, Qt::Orientation, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        switch (section) {
        case SIZE:
            return tr("Size");
        case OFFSET:
            return tr("Offset");
        case CODE:
            return tr("Code");
        case DATA:
            return tr("Data");
        case COMMENT:
            return tr("Comment");
        default:
            return QVariant();
        }
    default:
        return QVariant();
    }
}

RVA SearchModel::address(const QModelIndex &index) const
{
    const SearchDescription &exp = search->at(index.row());
    return exp.offset;
}

SearchSortFilterProxyModel::SearchSortFilterProxyModel(SearchModel *source_model, QObject *parent)
    : AddressableFilterProxyModel(source_model, parent)
{}

bool SearchSortFilterProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    QModelIndex index = sourceModel()->index(row, 0, parent);
    SearchDescription search
        = index.data(SearchModel::SearchDescriptionRole).value<SearchDescription>();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return search.code.contains(this->filterRegularExpression());
#else
    return search.code.contains(filterRegExp());
#endif
}

bool SearchSortFilterProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    SearchDescription left_search
        = left.data(SearchModel::SearchDescriptionRole).value<SearchDescription>();
    SearchDescription right_search
        = right.data(SearchModel::SearchDescriptionRole).value<SearchDescription>();

    switch (left.column()) {
    case SearchModel::SIZE:
        return left_search.size < right_search.size;
    case SearchModel::OFFSET:
        return left_search.offset < right_search.offset;
    case SearchModel::CODE:
        return left_search.code < right_search.code;
    case SearchModel::DATA:
        return left_search.data < right_search.data;
    case SearchModel::COMMENT:
        return Core()->getCommentAt(left_search.offset) < Core()->getCommentAt(right_search.offset);
    default:
        break;
    }

    return left_search.offset < right_search.offset;
}

SearchWidget::SearchWidget(MainWindow *main)
    : IaitoDockWidget(main)
    , ui(new Ui::SearchWidget)
{
    ui->setupUi(this);
    setStyleSheet(QStringLiteral("QToolTip { max-width: %1px; opacity: 230; }").arg(kMaxTooltipWidth));

    updateSearchBoundaries();

    search_model = new SearchModel(&search, this);
    search_proxy_model = new SearchSortFilterProxyModel(search_model, this);
    ui->searchTreeView->setModel(search_proxy_model);
    ui->searchTreeView->setMainWindow(main);
    ui->searchTreeView->sortByColumn(SearchModel::OFFSET, Qt::AscendingOrder);

    setScrollMode();

    connect(Core(), &IaitoCore::toggleDebugView, this, &SearchWidget::updateSearchBoundaries);
    connect(Core(), &IaitoCore::refreshAll, this, &SearchWidget::refreshSearchspaces);
    connect(Core(), &IaitoCore::commentsChanged, this, [this]() {
        qhelpers::emitColumnChanged(search_model, SearchModel::COMMENT);
    });

    QShortcut *enter_press = new QShortcut(QKeySequence(Qt::Key_Return), this);
    connect(enter_press, &QShortcut::activated, this, [this]() {
        refreshSearch();
        checkSearchResultEmpty();
    });
    enter_press->setContext(Qt::WidgetWithChildrenShortcut);

    connect(ui->searchButton, &QAbstractButton::clicked, this, [this]() {
        refreshSearch();
        checkSearchResultEmpty();
    });

    connect(
        ui->searchspaceCombo,
        static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this,
        [this](int index) { updatePlaceholderText(index); });
}

SearchWidget::~SearchWidget() {}

void SearchWidget::updateSearchBoundaries()
{
    QMap<QString, QString>::const_iterator mapIter;
    QMap<QString, QString> boundaries;

    if (Core()->currentlyDebugging && !Core()->currentlyEmulating) {
        boundaries = searchBoundariesDebug;
    } else {
        boundaries = searchBoundaries;
    }

    mapIter = boundaries.cbegin();
    int pos = ui->searchInCombo->findData(mapIter.key());
    if (pos < 0) {
        pos = 0;
    }
    // ui->searchInCombo->setCurrentIndex(pos);
    Config()->setConfig("search.in", mapIter.key());

    ui->searchInCombo->blockSignals(true);
    ui->searchInCombo->clear();
    for (; mapIter != boundaries.cend(); ++mapIter) {
        ui->searchInCombo->addItem(mapIter.value(), mapIter.key());
    }
    ui->searchInCombo->model()->sort(0, Qt::DescendingOrder);
    ui->searchInCombo->blockSignals(false);
    ui->searchInCombo->setCurrentIndex(1);

    ui->filterLineEdit->clear();
}

void SearchWidget::searchChanged()
{
    refreshSearchspaces();
}

void SearchWidget::refreshSearchspaces()
{
    int cur_idx = ui->searchspaceCombo->currentIndex();
    if (cur_idx < 0) {
        cur_idx = 0;
    }

    ui->searchspaceCombo->clear();
    ui->searchspaceCombo->addItem(tr("asm code"), QVariant("/acj"));
    // ui->searchspaceCombo->addItem(tr("instruction type"), QVariant("/atj"));
    // ui->searchspaceCombo->addItem(tr("instruction family"),
    // QVariant("/afj"));
    ui->searchspaceCombo->addItem(tr("string"), QVariant("/j"));
    ui->searchspaceCombo->addItem(tr("wide string"), QVariant("/wj"));
    // ui->searchspaceCombo->addItem(tr("syscalls"),     QVariant("/asj"));
    // ui->searchspaceCombo->addItem(tr("code strings"),     QVariant("/azj"));
    //  ui->searchspaceCombo->addItem(tr("magic"),     QVariant("/mj"));
    ui->searchspaceCombo->addItem(tr("hex string"), QVariant("/xj"));
    // ui->searchspaceCombo->addItem(tr("esil xrefs"), QVariant("/re"));
    ui->searchspaceCombo->addItem(tr("ROP gadgets"), QVariant("/Rj"));
    ui->searchspaceCombo->addItem(tr("64bit value"), QVariant("/v8j"));
    ui->searchspaceCombo->addItem(tr("32bit value"), QVariant("/v4j"));
    ui->searchspaceCombo->addItem(tr("16bit value"), QVariant("/v2j"));
    ui->searchspaceCombo->addItem(tr("8bit value"), QVariant("/v1j"));

    if (cur_idx > 0) {
        ui->searchspaceCombo->setCurrentIndex(cur_idx);
    }

    refreshSearch();
}

void SearchWidget::refreshSearch()
{
    QString search_for = ui->filterLineEdit->text();
    QVariant searchspace_data = ui->searchspaceCombo->currentData();
    QString searchspace = searchspace_data.toString();

    search_model->beginResetModel();
    search = Core()->getAllSearch(search_for, searchspace);
    search_model->endResetModel();

    qhelpers::adjustColumns(ui->searchTreeView, 3, 0);
}

// No Results Found information message when search returns empty
// Called by &QShortcut::activated and &QAbstractButton::clicked signals
void SearchWidget::checkSearchResultEmpty()
{
    if (search.isEmpty()) {
        QString noResultsMessage = "<b>";
        noResultsMessage.append(tr("No results found for:"));
        noResultsMessage.append("</b><br>");
        noResultsMessage.append(ui->filterLineEdit->text().toHtmlEscaped());
        QMessageBox::information(this, tr("No Results Found"), noResultsMessage);
    }
}

void SearchWidget::setScrollMode()
{
    qhelpers::setVerticalScrollMode(ui->searchTreeView);
}

void SearchWidget::updatePlaceholderText(int index)
{
    return;
#if 0
    switch (index) {
    case 1: // string
        ui->filterLineEdit->setPlaceholderText("foobar");
        break;
    case 2: // hex string
        ui->filterLineEdit->setPlaceholderText("deadbeef");
        break;
    case 3: // ROP gadgets
        ui->filterLineEdit->setPlaceholderText("pop,,pop");
        break;
    case 4: // 32bit value
        ui->filterLineEdit->setPlaceholderText("0xdeadbeef");
        break;
    default:
        ui->filterLineEdit->setPlaceholderText("jmp rax");
    }
#endif
}

void SearchWidget::on_searchInCombo_currentIndexChanged(int index)
{
    Config()->setConfig("search.in", ui->searchInCombo->itemData(index).toString());
}
