#include "HexdumpWidget.h"
#include "common/ShortcutManager.h"
#include "ui_HexdumpWidget.h"

#include "common/Configuration.h"
#include "common/Helpers.h"
#include "common/SyntaxHighlighter.h"
#include "common/TempConfig.h"
#include "core/MainWindow.h"

#include <cstring>
#include <QByteArray>
#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenu>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStringList>
#include <QTextDocumentFragment>
#include <QTextOption>
#include <QtEndian>

namespace {
enum StructureColumn { FieldColumn, TypeColumn, ValueColumn, AddressColumn, SizeColumn };
enum StructureRole { AddressRole = Qt::UserRole, SizeRole, PopulatedRole, PathRole };

QString arrayType(const QString &element, quint64 count)
{
    QString type = element;
    const int bracket = type.indexOf(QLatin1Char('['));
    type.insert(bracket < 0 ? type.size() : bracket, QStringLiteral("[%1]").arg(count));
    return type;
}

QColor fieldColor(const QString &path)
{
    // Stable across reloads, processes and themes; alpha preserves readable text.
    quint32 hash = 2166136261U;
    for (const QChar ch : path) {
        hash = (hash ^ ch.unicode()) * 16777619U;
    }
    QColor color = QColor::fromHsv(hash % 360, 160, 220);
    color.setAlpha(65);
    return color;
}
} // namespace

HexdumpWidget::HexdumpWidget(MainWindow *main)
    : MemoryDockWidget(MemoryWidgetType::Hexdump, main)
    , ui(new Ui::HexdumpWidget)
{
    ui->setupUi(this);
    // use character-level wrapping in art (histogram) tab
    ui->histogram->setWordWrapMode(QTextOption::WrapAnywhere);
    // Connect hex view cursor movement to status bar message
    auto updateStatus = [this](uint64_t addr) {
        QString text = RAddressString(addr);
        QString func = Core()->cmdFunctionAt(addr);
        if (!func.isEmpty()) {
            text += QStringLiteral(" ") + func;
        }
        auto sel = ui->hexTextView->getSelection();
        if (!sel.empty) {
            uint64_t count = sel.endAddress - sel.startAddress + 1;
            text += QStringLiteral(" [%1 bytes: %2-%3]")
                        .arg(QString::number(count))
                        .arg(RAddressString(sel.startAddress))
                        .arg(RAddressString(sel.endAddress));
        }
        if (mainWindow) {
            mainWindow->statusBar()->showMessage(text);
        }
    };
    connect(ui->hexTextView, &HexWidget::positionChanged, this, updateStatus);
    connect(
        ui->hexTextView,
        &HexWidget::selectionChanged,
        this,
        [this, updateStatus](HexWidget::Selection sel) {
            updateStatus(sel.empty ? current_address : sel.startAddress);
        });
    // Layout for Art tab: combobox and histogram
    ui->comboBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->histogram->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout *artLayout = new QVBoxLayout(ui->tabHistogram);
    artLayout->setContentsMargins(0, 0, 0, 0);
    artLayout->addWidget(ui->comboBox);
    artLayout->addWidget(ui->histogram);
    // Update Art tab when the selection in the combobox changes
    connect(
        ui->comboBox,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &HexdumpWidget::refreshSelectionInfo);

    setObjectName(main ? main->getUniqueObjectName(getWidgetType()) : getWidgetType());
    updateWindowTitle();

    ui->copyMD5->setIcon(QIcon(":/img/icons/copy.svg"));
    ui->copySHA1->setIcon(QIcon(":/img/icons/copy.svg"));
    ui->copySHA256->setIcon(QIcon(":/img/icons/copy.svg"));
    ui->copyCRC32->setIcon(QIcon(":/img/icons/copy.svg"));

    ui->splitter->setChildrenCollapsible(false);
    // A zero width handle overlaps the hex view and blocks accelerated scrolling
    ui->splitter->setHandleWidth(4);

    setMinimumSize(0, 0);
    ui->hexSideTab_2->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);

    QToolButton *closeButton = new QToolButton;
    QIcon closeIcon = QIcon(":/img/icons/delete.svg");
    closeButton->setIcon(closeIcon);
    closeButton->setAutoRaise(true);

    ui->hexSideTab_2->setCornerWidget(closeButton);
    syntaxHighLighter = Config()->createSyntaxHighlighter(ui->hexDisasTextEdit->document());

    ui->openSideViewB->hide(); // hide button at startup since side view is visible

    connect(closeButton, &QToolButton::clicked, this, [this] { showSidePanel(false); });

    connect(ui->openSideViewB, &QToolButton::clicked, this, [this] { showSidePanel(true); });

    // Set placeholders for the line-edit components
    QString placeholder = tr("Select bytes to display information");
    ui->bytesMD5->setPlaceholderText(placeholder);
    ui->bytesEntropy->setPlaceholderText(placeholder);
    ui->bytesSHA1->setPlaceholderText(placeholder);
    ui->bytesSHA256->setPlaceholderText(placeholder);
    ui->bytesCRC32->setPlaceholderText(placeholder);
    ui->hexDisasTextEdit->setPlaceholderText(placeholder);
    // Write values back to memory when editing is finished
    connect(ui->v_int8, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_int8, 1, true);
    });
    connect(ui->v_uint8, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_uint8, 1, false);
    });
    connect(ui->v_int16, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_int16, 2, true);
    });
    connect(ui->v_uint16, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_uint16, 2, false);
    });
    connect(ui->v_int24, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_int24, 3, true);
    });
    connect(ui->v_uint24, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_uint24, 3, false);
    });
    connect(ui->v_int32, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_int32, 4, true);
    });
    connect(ui->v_uint32, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_uint32, 4, false);
    });
    connect(ui->v_int48, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_int48, 6, true);
    });
    connect(ui->v_uint48, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_uint48, 6, false);
    });
    connect(ui->v_int64, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_int64, 8, true);
    });
    connect(ui->v_uint64, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_uint64, 8, false);
    });

    // ui->histogram->setLayout(ui->tabHistogram->layout());

    setupFonts();

    ui->openSideViewB->setStyleSheet(QStringLiteral(
                                         "QToolButton { border: 0px; padding: 0px; margin: 0px; }"
                                         "QToolButton:hover { border: 1px solid %1; }")
                                         .arg(palette().color(QPalette::Highlight).name()));

    refreshDeferrer = createReplacingRefreshDeferrer<RVA>(false, [this](const RVA *offset) {
        refresh(offset ? *offset : RVA_INVALID);
    });

    this->ui->hexTextView->addAction(&syncAction);

    connect(
        ui->hexTextView,
        &HexWidget::contextMenuAboutToShow,
        this,
        [this](QMenu *menu, RVA start, RVA end) {
            menu->addSeparator();
            menu->addAction(
                start == end ? tr("Analyze data here") : tr("Analyze selected data"),
                this,
                [this, start, end]() {
                    const RVA last = start == end ? start
                                                        + qMin<RVA>(
                                                            qMax<RVA>(1, Core()->core()->blocksize),
                                                            RVA_MAX - start)
                                                        - 1
                                                  : end;
                    mainWindow->showDataAnalysis(start, last);
                });
        });

    connect(Config(), &Configuration::fontsUpdated, this, &HexdumpWidget::fontsUpdated);
    connect(Core(), &IaitoCore::refreshAll, this, [this]() { refresh(); });
    connect(Core(), &IaitoCore::refreshCodeViews, this, [this]() { refresh(); });
    connect(Core(), &IaitoCore::instructionChanged, this, [this]() { refresh(); });
    connect(Core(), &IaitoCore::stackChanged, this, [this]() { refresh(); });
    connect(Core(), &IaitoCore::registersChanged, this, [this]() { refresh(); });
    connect(
        Core(),
        &IaitoCore::addressRangeSelectionChanged,
        this,
        &HexdumpWidget::applyAddressRangeSelection);

    connect(seekable, &IaitoSeekable::seekableSeekChanged, this, &HexdumpWidget::onSeekChanged);
    connect(ui->hexTextView, &HexWidget::positionChanged, this, [this](RVA addr) {
        if (!sent_seek) {
            sent_seek = true;
            seekable->seek(addr);
            sent_seek = false;
        }
        this->current_address = addr;
    });
    // Also update parse/value pane when cursor moves without selection
    connect(ui->hexTextView, &HexWidget::positionChanged, this, &HexdumpWidget::refreshSelectionInfo);
    connect(
        ui->hexTextView, &HexWidget::selectionChanged, this, &HexdumpWidget::onHexSelectionChanged);
    connect(ui->hexSideTab_2, &QTabWidget::currentChanged, this, &HexdumpWidget::refreshSelectionInfo);
    ui->hexTextView->installEventFilter(this);
    // Monitor side tab resize to update value display
    ui->hexSideTab_2->installEventFilter(this);

    initParsing();
    initStructures();
    // Populate Format tab struct selector and inline format
    {
        // Load type names from radare2
        QString typesOutput = Core()->cmdRaw("pf.~[0]"); // ts");
        QStringList typesList = typesOutput.split('\n', Qt::SkipEmptyParts);
        ui->formatStructComboBox->addItem(QString());
        for (const QString &typeName : typesList) {
            ui->formatStructComboBox->addItem(typeName);
        }
        ui->formatInlineEdit->setPlaceholderText(tr("Enter struct or format spec"));
        connect(
            ui->formatStructComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &HexdumpWidget::refreshSelectionInfo);
        connect(
            ui->formatInlineEdit,
            &QLineEdit::editingFinished,
            this,
            &HexdumpWidget::refreshSelectionInfo);
    }
    selectHexPreview();

    // apply initial offset
    refresh(seekable->getOffset());
    {
        auto make_nav = [&](const QString &id, int arrowKey, Qt::KeyboardModifiers mod) {
            QShortcut *sc
                = ShortcutMgr()->registerShortcut(id, this, Qt::WidgetWithChildrenShortcut);
            connect(sc, &QShortcut::activated, this, [this, arrowKey, mod]() {
                QKeyEvent ke(QEvent::KeyPress, arrowKey, mod);
                QCoreApplication::sendEvent(ui->hexTextView, &ke);
            });
        };
        make_nav("hex.navLeft", Qt::Key_Left, Qt::NoModifier);
        make_nav("hex.navDown", Qt::Key_Down, Qt::NoModifier);
        make_nav("hex.navUp", Qt::Key_Up, Qt::NoModifier);
        make_nav("hex.navRight", Qt::Key_Right, Qt::NoModifier);
        make_nav("hex.navLeftSelect", Qt::Key_Left, Qt::ShiftModifier);
        make_nav("hex.navDownSelect", Qt::Key_Down, Qt::ShiftModifier);
        make_nav("hex.navUpSelect", Qt::Key_Up, Qt::ShiftModifier);
        make_nav("hex.navRightSelect", Qt::Key_Right, Qt::ShiftModifier);
    }
}

// Event filter to catch resize events on the side tabs and refresh values
bool HexdumpWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize && watched == ui->hexSideTab_2) {
        refreshSelectionInfo();
    }
    return MemoryDockWidget::eventFilter(watched, event);
}

// Helper to write edited value fields back to memory
void HexdumpWidget::writeValueEdit(QLineEdit *editor, int byteCount, bool isSigned)
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    bool ok = false;
    quint64 uValue = 0;
    if (isSigned) {
        qint64 signedValue = editor->text().toLongLong(&ok);
        if (!ok) {
            return;
        }
        if (byteCount < 8) {
            quint64 mask = (1ULL << (byteCount * 8)) - 1;
            uValue = static_cast<quint64>(signedValue) & mask;
        } else {
            uValue = static_cast<quint64>(signedValue);
        }
    } else {
        uValue = editor->text().toULongLong(&ok);
        if (!ok) {
            return;
        }
        if (byteCount < 8) {
            quint64 mask = (1ULL << (byteCount * 8)) - 1;
            uValue &= mask;
        }
    }
    // Format as hex with fixed width
    QString hex = QString::number(uValue, 16).rightJustified(byteCount * 2, QLatin1Char('0'));
    // Handle endianness: little endian reverses byte order
    bool bigEndian = ui->valueEndian->currentIndex() == 1;
    if (!bigEndian) {
        QString rev;
        for (int i = hex.length(); i > 0; i -= 2) {
            rev += hex.mid(i - 2, 2);
        }
        hex = rev;
    }
    Core()->editBytes(this->current_address, hex);
}

void HexdumpWidget::onSeekChanged(RVA addr)
{
    this->current_address = addr;
    if (sent_seek) {
        sent_seek = false;
        return;
    }
    refresh(addr);
}

HexdumpWidget::~HexdumpWidget() {}

QString HexdumpWidget::getWidgetType()
{
    return "Hexdump";
}

void HexdumpWidget::refresh()
{
    refresh(RVA_INVALID);
}

void HexdumpWidget::refresh(RVA addr)
{
    if (!refreshDeferrer->attemptRefresh(addr == RVA_INVALID ? nullptr : new RVA(addr))) {
        return;
    }
    sent_seek = true;
    if (addr != RVA_INVALID) {
        ui->hexTextView->seek(addr);
    } else {
        ui->hexTextView->refresh();
        refreshSelectionInfo();
    }
    // this->current_address = ui->hexTextView->cursor.address;
    sent_seek = false;
    if (addr == RVA_INVALID) {
        reloadStructures();
    }
}

void HexdumpWidget::initParsing()
{
    // Fill the plugins combo for the hexdump sidebar
    ui->parseTypeComboBox->addItem(tr("Disassembly"), "pda");
    ui->parseTypeComboBox->addItem(tr("String"), "pcs");
    ui->parseTypeComboBox->addItem(tr("Assembler"), "pca");
    ui->parseTypeComboBox->addItem(tr("C bytes"), "pc");
    ui->parseTypeComboBox->addItem(tr("C bytes with instructions"), "pci");
    ui->parseTypeComboBox->addItem(tr("C half-words (2 byte)"), "pch");
    ui->parseTypeComboBox->addItem(tr("C words (4 byte)"), "pcw");
    ui->parseTypeComboBox->addItem(tr("C dwords (8 byte)"), "pcd");
    ui->parseTypeComboBox->addItem(tr("Python"), "pcp");
    ui->parseTypeComboBox->addItem(tr("JSON"), "pcj");
    ui->parseTypeComboBox->addItem(tr("JavaScript"), "pcJ");
    ui->parseTypeComboBox->addItem(tr("Yara"), "pcy");

    ui->parseArchComboBox->insertItems(0, Core()->getAsmPluginNames());

    ui->parseEndianComboBox->setCurrentIndex(Core()->getConfigb("cfg.bigendian") ? 1 : 0);
}

void HexdumpWidget::onHexSelectionChanged(HexWidget::Selection selection)
{
    if (!applyingAddressRangeSelection && !sent_seek
        && Config()->getAddressRangeSelectionSyncEnabled()) {
        publishingAddressRangeSelection = true;
        if (selection.empty) {
            Core()->clearAddressRangeSelection();
        } else {
            Core()->setAddressRangeSelection(selection.startAddress, selection.endAddress);
        }
        publishingAddressRangeSelection = false;
    }

    selectionChanged(selection);
}

void HexdumpWidget::selectionChanged(HexWidget::Selection selection)
{
    if (selection.empty) {
        clearParseWindow();
        return;
    }
    if (selection.endAddress < selection.startAddress) {
        return;
    }
    updateParseWindow(selection.startAddress, selection.endAddress);
}

void HexdumpWidget::applyAddressRangeSelection(RVA start, RVA end)
{
    if (publishingAddressRangeSelection) {
        return;
    }
    const bool hasRange = start != RVA_INVALID && end != RVA_INVALID && start <= end;

    applyingAddressRangeSelection = true;
    sent_seek = true;
    if (hasRange) {
        ui->hexTextView->selectRange(start, end);
    } else {
        ui->hexTextView->clearSelection();
    }
    sent_seek = false;
    applyingAddressRangeSelection = false;
    refreshSelectionInfo();
}

void HexdumpWidget::on_parseArchComboBox_currentTextChanged(const QString & /*arg1*/)
{
    refreshSelectionInfo();
}

void HexdumpWidget::on_parseBitsComboBox_currentTextChanged(const QString & /*arg1*/)
{
    refreshSelectionInfo();
}

void HexdumpWidget::setupFonts()
{
    QFont font = Config()->getFont();
    ui->hexDisasTextEdit->setFont(font);
    ui->hexTextView->setMonospaceFont(font);
    ui->formatOutput->setFont(font);
    ui->structureTree->setFont(font);
    ui->structurePinsTree->setFont(font);
    ui->structureCommandOutput->setFont(font);
}

void HexdumpWidget::refreshSelectionInfo()
{
    // Guard against re-entry: updateParseWindow calls setPlainText on a
    // widget inside hexSideTab_2, and any resulting resize of the tab fires
    // the eventFilter hook which calls back into refreshSelectionInfo.
    if (refreshingSelection) {
        return;
    }
    refreshingSelection = true;
    selectionChanged(ui->hexTextView->getSelection());
    refreshingSelection = false;
}

void HexdumpWidget::fontsUpdated()
{
    setupFonts();
}

void HexdumpWidget::initStructures()
{
    ui->structureTypeComboBox->setInsertPolicy(QComboBox::NoInsert);
    ui->structureAddressEdit->setText(RAddressString(seekable->getOffset()));
    ui->structureCommandOutput->hide();
    ui->structureTree->header()->setStretchLastSection(false);
    for (int column = FieldColumn; column <= SizeColumn; ++column) {
        ui->structureTree->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    qhelpers::setVerticalScrollMode(ui->structureTree);
    ui->structureTree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->structureSplitter->setStretchFactor(0, 3);
    ui->structureSplitter->setStretchFactor(1, 1);
    connect(ui->hexSideTab_2, &QTabWidget::currentChanged, this, [this]() {
        if (ui->hexSideTab_2->currentWidget() == ui->tabStructures && !structurePanelSized) {
            const int width = ui->splitter->width();
            ui->splitter->setSizes({width / 2, width / 2});
            structurePanelSized = true;
        }
    });
    connect(ui->structureInspectButton, &QPushButton::clicked, this, &HexdumpWidget::inspectStructure);
    connect(
        ui->structureAddressEdit, &QLineEdit::returnPressed, this, &HexdumpWidget::inspectStructure);
    connect(
        ui->structureTypeComboBox->lineEdit(),
        &QLineEdit::returnPressed,
        this,
        &HexdumpWidget::inspectStructure);
    connect(ui->structureRefreshButton, &QPushButton::clicked, this, &HexdumpWidget::reloadStructures);
    connect(ui->structurePinButton, &QPushButton::clicked, this, &HexdumpWidget::pinStructure);
    connect(ui->structureCommandButton, &QPushButton::clicked, this, &HexdumpWidget::pinCommand);
    connect(ui->structureUnpinButton, &QPushButton::clicked, this, &HexdumpWidget::removeStructurePin);
    connect(ui->structureCursorButton, &QPushButton::clicked, this, [this]() {
        const auto selection = ui->hexTextView->getSelection();
        const RVA address = selection.empty ? current_address : selection.startAddress;
        ui->structureAddressEdit->setText(RAddressString(address));
        inspectStructure();
    });
    connect(ui->structureTree, &QTreeWidget::itemExpanded, this, &HexdumpWidget::populateStructure);
    connect(ui->structureTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        selectStructureField(item);
    });
    connect(
        ui->structureTree, &QTreeWidget::itemActivated, this, &HexdumpWidget::selectStructureField);
    connect(ui->structureTree, &QTreeWidget::itemCollapsed, this, [this]() {
        updateStructureHighlights();
    });
    connect(
        ui->structurePinsTree,
        &QTreeWidget::itemActivated,
        this,
        &HexdumpWidget::activateStructurePin);
    connect(
        ui->structurePinsTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
            ui->structureUnpinButton->setEnabled(item != nullptr);
            ui->structureRunButton->setEnabled(item && item->data(1, Qt::UserRole).toBool());
            ui->structureCommandOutput->clear();
            ui->structureCommandOutput->hide();
        });
    connect(ui->structureRunButton, &QPushButton::clicked, this, [this]() {
        auto *item = ui->structurePinsTree->currentItem();
        if (!item || !item->data(1, Qt::UserRole).toBool()) {
            return;
        }
        const RVA address = item->data(0, Qt::UserRole).toULongLong();
        ui->structureCommandOutput->show();
        ui->structureCommandOutput->setPlainText(
            Core()->cmdRawAt(Core()->getCommandAnnotation(address), address).trimmed());
    });
    connect(ui->structureTree, &QTreeWidget::customContextMenuRequested, this, [this](QPoint pos) {
        auto *item = ui->structureTree->itemAt(pos);
        if (!item || !item->data(AddressColumn, AddressRole).isValid()) {
            return;
        }
        QMenu menu(this);
        menu.addAction(tr("Select field bytes"), this, [this, item]() {
            selectStructureField(item);
        });
        menu.addAction(tr("Copy value"), this, [item]() {
            QGuiApplication::clipboard()->setText(item->text(ValueColumn));
        });
        menu.addAction(tr("Copy address"), this, [item]() {
            QGuiApplication::clipboard()->setText(item->text(AddressColumn));
        });
        menu.exec(ui->structureTree->viewport()->mapToGlobal(pos));
    });
    connect(ui->structurePinsTree, &QTreeWidget::customContextMenuRequested, this, [this](QPoint pos) {
        auto *item = ui->structurePinsTree->itemAt(pos);
        if (!item) {
            return;
        }
        ui->structurePinsTree->setCurrentItem(item);
        QMenu menu(this);
        menu.addAction(tr("Go to pinned address"), this, [this, item]() {
            activateStructurePin(item);
        });
        menu.addAction(tr("Unpin"), this, &HexdumpWidget::removeStructurePin);
        menu.exec(ui->structurePinsTree->viewport()->mapToGlobal(pos));
    });
    connect(
        ui->hexTextView,
        &HexWidget::contextMenuAboutToShow,
        this,
        [this](QMenu *menu, RVA start, RVA) {
            menu->addAction(tr("Inspect structure here"), this, [this, start]() {
                showSidePanel(true);
                ui->hexSideTab_2->setCurrentWidget(ui->tabStructures);
                reloadStructures();
                RVA base = start;
                const auto links = typeLayout.links();
                for (auto it = links.cbegin(); it != links.cend(); ++it) {
                    const RVA size = typeLayout.size(it.value());
                    if (start >= it.key() && start - it.key() < size) {
                        base = it.key();
                        ui->structureTypeComboBox->setCurrentText(it.value());
                    }
                }
                ui->structureAddressEdit->setText(RAddressString(base));
                inspectStructure();
            });
        });
    reloadStructures();
}

void HexdumpWidget::reloadStructures()
{
    if (updatingStructures) {
        return;
    }
    typeLayout.load(Core()->cmdRaw("tk"), Core()->getConfigi("asm.bits") / 8);
    const QString selectedType = ui->structureTypeComboBox->currentText();
    {
        QSignalBlocker blocker(ui->structureTypeComboBox);
        ui->structureTypeComboBox->clear();
        ui->structureTypeComboBox->addItem(QString());
        ui->structureTypeComboBox->addItems(typeLayout.names());
        ui->structureTypeComboBox->setCurrentText(selectedType);
    }
    auto *selected = ui->structurePinsTree->currentItem();
    const QVariant selectedAddress = selected ? selected->data(0, Qt::UserRole) : QVariant();
    const bool selectedCommand = selected && selected->data(1, Qt::UserRole).toBool();
    ui->structurePinsTree->clear();
    const auto addPin = [this,
                         &selectedAddress,
                         selectedCommand](RVA address, const QString &text, bool command) {
        auto *item = new QTreeWidgetItem(ui->structurePinsTree, {RAddressString(address), text});
        item->setData(0, Qt::UserRole, QVariant::fromValue(address));
        item->setData(1, Qt::UserRole, command);
        item->setToolTip(
            1,
            command ? tr("Command annotation (Cr): %1").arg(text)
                    : tr("Type link (tl): %1").arg(text));
        if (selectedAddress.isValid() && selectedAddress.toULongLong() == address
            && selectedCommand == command) {
            ui->structurePinsTree->setCurrentItem(item);
        }
    };
    const auto links = typeLayout.links();
    for (auto it = links.cbegin(); it != links.cend(); ++it) {
        addPin(it.key(), it.value(), false);
    }
    // Read addresses as text rather than JSON doubles, including addresses above 2^53.
    static const QRegularExpression commandPin(
        QStringLiteral("^Cr\\s+\\d+\\s+(0x[0-9a-fA-F]+)\\s+#"));
    for (const QString &line : Core()->cmdRaw("Cr*").split(QLatin1Char('\n'))) {
        const auto match = commandPin.match(line);
        if (match.hasMatch()) {
            bool ok = false;
            const RVA address = match.captured(1).toULongLong(&ok, 16);
            if (ok) {
                addPin(address, Core()->getCommandAnnotation(address), true);
            }
        }
    }
    ui->structurePinsTree->resizeColumnToContents(0);
    if (!structureType.isEmpty()) {
        inspectStructure();
    }
}

void HexdumpWidget::inspectStructure()
{
    const QString type = ui->structureTypeComboBox->currentText().trimmed();
    const QString expression = ui->structureAddressEdit->text().trimmed();
    RVA address = current_address;
    const bool validAddress = expression.isEmpty() || Core()->tryMath(expression, address);
    QSet<QString> expanded;
    QString selectedPath;
    const bool sameStructure = type == structureType && validAddress && address == structureAddress
                               && ui->structureTree->topLevelItemCount() > 0;
    if (sameStructure) {
        const auto remember = [&expanded](const auto &self, QTreeWidgetItem *item) -> void {
            if (item->isExpanded()) {
                expanded.insert(item->data(FieldColumn, PathRole).toString());
            }
            for (int i = 0; i < item->childCount(); ++i) {
                self(self, item->child(i));
            }
        };
        for (int i = 0; i < ui->structureTree->topLevelItemCount(); ++i) {
            remember(remember, ui->structureTree->topLevelItem(i));
        }
        if (auto *selected = ui->structureTree->currentItem()) {
            selectedPath = selected->data(FieldColumn, PathRole).toString();
        }
    }
    QSignalBlocker blocker(ui->structureTree);
    ui->structureTree->clear();
    ui->hexTextView->setTypeRanges({});
    structureType = type;
    structureAddress = validAddress ? address : RVA_INVALID;
    if (!typeLayout.size(structureType)) {
        ui->structureStatusLabel->setText(tr("Choose a known type with a valid, finite layout."));
        return;
    }
    if (!validAddress || !Core()->isAddressMapped(structureAddress)) {
        ui->structureStatusLabel->setText(tr("Enter a valid address expression in mapped memory."));
        return;
    }
    auto *root = addStructureField(
        ui->structureTree->invisibleRootItem(), structureType, structureType, structureAddress);
    populateStructure(root);
    root->setExpanded(
        !sameStructure || expanded.contains(root->data(FieldColumn, PathRole).toString()));
    const auto restore =
        [this, &expanded, &selectedPath](const auto &self, QTreeWidgetItem *item) -> void {
        const QString path = item->data(FieldColumn, PathRole).toString();
        if (expanded.contains(path)) {
            populateStructure(item);
            item->setExpanded(true);
        }
        if (!selectedPath.isEmpty() && path == selectedPath) {
            ui->structureTree->setCurrentItem(item);
        }
        for (int i = 0; i < item->childCount(); ++i) {
            self(self, item->child(i));
        }
    };
    restore(restore, root);
    updateStructureHighlights();
    ui->structureStatusLabel->setText(
        tr("%1 at %2. Expand fields; select a row to highlight its bytes.")
            .arg(structureType, RAddressString(structureAddress)));
}

QTreeWidgetItem *HexdumpWidget::addStructureField(
    QTreeWidgetItem *parent, const QString &name, const QString &type, RVA address)
{
    auto *item = new QTreeWidgetItem(parent);
    const RVA size = typeLayout.size(type);
    item->setText(FieldColumn, name);
    item->setText(TypeColumn, type);
    item->setText(AddressColumn, RAddressString(address));
    item->setText(SizeColumn, QString::number(size));
    const QString path = parent->data(FieldColumn, PathRole).toString() + QLatin1Char('.') + name;
    item->setData(FieldColumn, PathRole, path);
    item->setBackground(FieldColumn, fieldColor(path));
    if (!size || size - 1 > RVA_MAX - address) {
        item->setText(ValueColumn, tr("Invalid layout / address overflow"));
        return item;
    }
    item->setData(AddressColumn, AddressRole, QVariant::fromValue(address));
    item->setData(SizeColumn, SizeRole, QVariant::fromValue(size));
    const QString kind = typeLayout.kind(type);
    bool expandable = kind == QLatin1String("struct") || kind == QLatin1String("union")
                      || kind == QLatin1String("array") || kind == QLatin1String("pointer");
    if (kind != QLatin1String("struct") && kind != QLatin1String("union")
        && kind != QLatin1String("array")) {
        const int length = int(qMin<RVA>(size, 16));
        if (!Core()->isAddressMapped(address) || !Core()->isAddressMapped(address + length - 1)) {
            item->setText(ValueColumn, tr("Unmapped"));
            expandable = false;
        } else {
            const QByteArray bytes = Core()->ioRead(address, length);
            item->setText(
                ValueColumn,
                bytes.size() == length
                    ? typeLayout.value(type, bytes, Core()->getConfigb("cfg.bigendian"))
                    : tr("Unreadable"));
            if (size > RVA(length)) {
                item->setText(ValueColumn, item->text(ValueColumn) + QStringLiteral(" …"));
            }
        }
    }
    int depth = 0;
    for (auto *ancestor = parent; ancestor; ancestor = ancestor->parent()) {
        ++depth;
        if (ancestor->data(AddressColumn, AddressRole).isValid()
            && ancestor->data(AddressColumn, AddressRole).toULongLong() == address
            && typeLayout.resolve(ancestor->text(TypeColumn)) == typeLayout.resolve(type)) {
            expandable = false;
            item->setText(ValueColumn, tr("Recursive reference"));
            break;
        }
    }
    if (depth >= 32) {
        expandable = false;
        item->setText(ValueColumn, tr("Depth limit reached"));
    }
    if (expandable) {
        new QTreeWidgetItem(item); // Expander; children are read only when opened.
    }
    for (int column = FieldColumn; column <= SizeColumn; ++column) {
        item->setToolTip(
            column,
            tr("%1\nType: %2\nAddress: %3\nSize: %4 bytes")
                .arg(path, type, RAddressString(address), QString::number(size)));
    }
    return item;
}

void HexdumpWidget::populateStructure(QTreeWidgetItem *item)
{
    if (!item || item->data(FieldColumn, PopulatedRole).toBool()
        || !item->data(AddressColumn, AddressRole).isValid() || item->childCount() == 0) {
        return;
    }
    item->setData(FieldColumn, PopulatedRole, true);
    qDeleteAll(item->takeChildren());
    const QString type = typeLayout.resolve(item->text(TypeColumn));
    const RVA address = item->data(AddressColumn, AddressRole).toULongLong();
    QString element;
    quint64 count;
    if (TypeLayout::array(type, &element, &count)) {
        const RVA stride = typeLayout.size(element);
        // Group large arrays hierarchically, keeping every element reachable.
        const quint64 groupSize = count > 256 ? count / 256 + (count % 256 != 0) : 1;
        for (quint64 index = 0; index < count;) {
            const quint64 groupCount = qMin(groupSize, count - index);
            addStructureField(
                item,
                groupCount == 1 ? QStringLiteral("[%1]").arg(index)
                                : QStringLiteral("[%1…%2]").arg(index).arg(index + groupCount - 1),
                groupCount == 1 ? element : arrayType(element, groupCount),
                address + index * stride);
            index += groupCount;
        }
    } else if (typeLayout.kind(type) == QLatin1String("pointer")) {
        const int size = int(typeLayout.size(type));
        const QByteArray bytes = Core()->ioRead(address, size);
        const RVA target = TypeLayout::unsignedValue(bytes, Core()->getConfigb("cfg.bigendian"));
        if (bytes.size() == size && target && Core()->isAddressMapped(target)) {
            addStructureField(
                item,
                QStringLiteral("*%1").arg(item->text(FieldColumn)),
                type.left(type.size() - 1).trimmed(),
                target);
        } else {
            item->setText(ValueColumn, item->text(ValueColumn) + tr(" (no mapped pointee)"));
        }
    } else {
        for (const auto &field : typeLayout.fields(type)) {
            addStructureField(
                item,
                field.name,
                field.count > 1 ? arrayType(field.type, field.count) : field.type,
                address + field.offset);
        }
    }
    updateStructureHighlights();
}

void HexdumpWidget::updateStructureHighlights()
{
    QVector<HexWidget::ColoredRange> ranges;
    const auto visit = [this, &ranges](const auto &self, QTreeWidgetItem *item) -> void {
        bool hasChildren = false;
        if (item->isExpanded() && item->data(FieldColumn, PopulatedRole).toBool()) {
            for (int i = 0; i < item->childCount(); ++i) {
                self(self, item->child(i));
                hasChildren = true;
            }
        }
        if ((!hasChildren || typeLayout.kind(item->text(TypeColumn)) == QLatin1String("pointer"))
            && item->data(AddressColumn, AddressRole).isValid()) {
            const RVA address = item->data(AddressColumn, AddressRole).toULongLong();
            const RVA size = item->data(SizeColumn, SizeRole).toULongLong();
            ranges.append(
                {address,
                 address + size - 1,
                 fieldColor(item->data(FieldColumn, PathRole).toString())});
        }
    };
    for (int i = 0; i < ui->structureTree->topLevelItemCount(); ++i) {
        visit(visit, ui->structureTree->topLevelItem(i));
    }
    ui->hexTextView->setTypeRanges(ranges);
}

void HexdumpWidget::selectStructureField(QTreeWidgetItem *item)
{
    if (!item || !item->data(AddressColumn, AddressRole).isValid()) {
        return;
    }
    const RVA address = item->data(AddressColumn, AddressRole).toULongLong();
    const RVA size = item->data(SizeColumn, SizeRole).toULongLong();
    // Keep the inspected root anchored while the hex cursor and seek move to a field.
    updatingStructures = true;
    sent_seek = true;
    ui->hexTextView->seek(address);
    ui->hexTextView->selectRange(address, address + size - 1);
    seekable->seek(address);
    sent_seek = false;
    updatingStructures = false;
    onHexSelectionChanged(ui->hexTextView->getSelection());
}

void HexdumpWidget::activateStructurePin(QTreeWidgetItem *item)
{
    if (!item) {
        return;
    }
    const RVA address = item->data(0, Qt::UserRole).toULongLong();
    seekable->seek(address);
    ui->structureAddressEdit->setText(RAddressString(address));
    if (!item->data(1, Qt::UserRole).toBool()) {
        ui->structureTypeComboBox->setCurrentText(item->text(1));
        inspectStructure();
    }
}

void HexdumpWidget::pinStructure()
{
    inspectStructure();
    const QString entered = ui->structureTypeComboBox->currentText().trimmed();
    const QString type = typeLayout.names().contains(entered) ? entered
                                                              : typeLayout.resolve(entered);
    static const QRegularExpression safeType(QStringLiteral("^[\\p{L}\\p{N}_ :*]+$"));
    if (ui->structureTree->topLevelItemCount() == 0 || !typeLayout.names().contains(type)
        || !safeType.match(type).hasMatch()) {
        ui->structureStatusLabel->setText(
            tr("Pin a named r2 type. Define a typedef to pin an array."));
        return;
    }
    Core()->cmdRaw(QStringLiteral("tl %1 = %2").arg(type, RAddressString(structureAddress)));
    emit Core() -> refreshCodeViews();
    reloadStructures();
}

void HexdumpWidget::pinCommand()
{
    const QString expression = ui->structureAddressEdit->text().trimmed();
    RVA address = current_address;
    if ((!expression.isEmpty() && !Core()->tryMath(expression, address))
        || !Core()->isAddressMapped(address)) {
        ui->structureStatusLabel->setText(tr("Enter a valid command address in mapped memory."));
        return;
    }
    bool accepted = false;
    const QString command = QInputDialog::getText(
                                this,
                                tr("Pin command"),
                                tr("r2 command at %1:").arg(RAddressString(address)),
                                QLineEdit::Normal,
                                Core()->getCommandAnnotation(address),
                                &accepted)
                                .trimmed();
    if (!accepted || command.isEmpty()) {
        return;
    }
    const auto selection = ui->hexTextView->getSelection();
    const RVA size = !selection.empty && selection.startAddress == address
                         ? selection.endAddress - address + 1
                         : qMax<RVA>(1, typeLayout.size(ui->structureTypeComboBox->currentText()));
    if (size - 1 > RVA_MAX - address) {
        return;
    }
    Core()->setCommandAnnotation(address, size, command);
    reloadStructures();
}

void HexdumpWidget::removeStructurePin()
{
    const auto *item = ui->structurePinsTree->currentItem();
    if (!item) {
        return;
    }
    const RVA address = item->data(0, Qt::UserRole).toULongLong();
    Core()->cmdRawAt(
        item->data(1, Qt::UserRole).toBool() ? "Cr-" : "tl- " + RAddressString(address), address);
    emit Core() -> refreshCodeViews();
    reloadStructures();
}

void HexdumpWidget::clearParseWindow()
{
    ui->hexDisasTextEdit->setPlainText("");
    ui->bytesEntropy->setText("");
    ui->bytesMD5->setText("");
    ui->bytesSHA1->setText("");
    ui->bytesSHA256->setText("");
    ui->bytesCRC32->setText("");
#if 0
    ui->v_int8->setText("");
    ui->v_uint8->setText("");
    ui->v_int16->setText("");
    ui->v_uint16->setText("");
    ui->v_int24->setText("");
    ui->v_uint24->setText("");
    ui->v_int32->setText("");
    ui->v_uint32->setText("");
    ui->v_int48->setText("");
    ui->v_uint48->setText("");
    ui->v_int64->setText("");
    ui->v_uint64->setText("");
    ui->v_uint64->setText("");
#endif
}

void HexdumpWidget::showSidePanel(bool show)
{
    ui->hexSideTab_2->setVisible(show);
    ui->openSideViewB->setHidden(show);
    if (show) {
        refreshSelectionInfo();
    }
}

QString HexdumpWidget::getWindowTitle() const
{
    return tr("Hexdump");
}

void HexdumpWidget::updateParseWindow(RVA start_address, RVA end_address)
{
    if (!ui->hexSideTab_2->isVisible()) {
        return;
    }
    if (end_address < start_address) {
        return;
    }
    // Cap at a sane ceiling so a bogus selection can't stall setPlainText.
    const qint64 kMaxSize = 1 << 20;
    qint64 size = static_cast<qint64>(end_address - start_address) - 1;
    if (size > kMaxSize) {
        size = kMaxSize;
    }
    uint64_t at = (size > 0 && this->current_address > start_address) ? this->current_address - 1
                                                                      : this->current_address;
    auto cmd = [start_address](const QString &c, uint64_t addr = (uint64_t) -1) {
        return Core()->cmdRawAt(c, (addr == (uint64_t) -1) ? start_address : addr).trimmed();
    };

    const int selectedTab = ui->hexSideTab_2->currentIndex();
    switch (selectedTab) {
    case 0: // disasm
        if (size > 0) {
            // Get selected combos
            QString arch = ui->parseArchComboBox->currentText();
            QString bits = ui->parseBitsComboBox->currentText();
            QString selectedCommand = ui->parseTypeComboBox->currentData().toString();
            QString commandResult = "";
            bool bigEndian = ui->parseEndianComboBox->currentIndex() == 1;

            TempConfig tempConfig;
            tempConfig.set("asm.arch", arch).set("asm.bits", bits).set("cfg.bigendian", bigEndian);

            ui->hexDisasTextEdit->setPlainText(
                selectedCommand != ""
                    ? Core()->cmdRawAt(
                          QStringLiteral("%1 %2").arg(selectedCommand).arg(size), start_address)
                    : "");
        }
        break;
    case 1: // hash
        if (size > 0) {
            // Fill the information tab hashes and entropy
            ui->bytesMD5->setText(
                Core()->cmdRawAt(QStringLiteral("ph md5 %1").arg(size), start_address).trimmed());
            ui->bytesSHA1->setText(
                Core()->cmdRawAt(QStringLiteral("ph sha1 %1").arg(size), start_address).trimmed());
            ui->bytesSHA256->setText(
                Core()->cmdRawAt(QStringLiteral("ph sha256 %1").arg(size), start_address).trimmed());
            ui->bytesCRC32->setText(
                Core()->cmdRawAt(QStringLiteral("ph crc32 %1").arg(size), start_address).trimmed());
            ui->bytesEntropy->setText(
                Core()->cmdRawAt(QStringLiteral("ph entropy %1").arg(size), start_address).trimmed());
            ui->bytesMD5->setCursorPosition(0);
            ui->bytesSHA1->setCursorPosition(0);
            ui->bytesSHA256->setCursorPosition(0);
            ui->bytesCRC32->setCursorPosition(0);
        }
        break;
    case 2: // values
    {
        // Display interpreted values from memory in various types
        bool bigEndian = ui->valueEndian->currentIndex() == 1;
        Core()->setConfig("cfg.bigendian", bigEndian);
        // Read up to 8 bytes from memory at the selected address
        QByteArray buf = Core()->ioRead(at, 8);
        // Pad buffer if not enough bytes
        while ((int) buf.size() < 8) {
            buf.append(char(0));
        }
        const uchar *data = reinterpret_cast<const uchar *>(buf.constData());
        // int8 / uint8
        qint8 v8s = *reinterpret_cast<const qint8 *>(data);
        quint8 v8u = data[0];
        // int16 / uint16
        quint16 raw16 = bigEndian ? qFromBigEndian<quint16>(data)
                                  : qFromLittleEndian<quint16>(data);
        qint16 v16s = *reinterpret_cast<const qint16 *>(&raw16);
        quint16 v16u = raw16;
        // int24 / uint24
        quint32 raw24 = bigEndian
                            ? (quint32(data[0]) << 16 | quint32(data[1]) << 8 | quint32(data[2]))
                            : (quint32(data[2]) << 16 | quint32(data[1]) << 8 | quint32(data[0]));
        qint32 v24s = (raw24 & 0x800000) ? qint32(raw24 | 0xFF000000) : qint32(raw24);
        quint32 v24u = raw24;
        // int32 / uint32
        quint32 raw32 = bigEndian ? qFromBigEndian<quint32>(data)
                                  : qFromLittleEndian<quint32>(data);
        qint32 v32s = *reinterpret_cast<const qint32 *>(&raw32);
        quint32 v32u = raw32;
        // int48 / uint48
        quint64 raw48 = 0;
        if (bigEndian) {
            for (int i = 0; i < 6; i++) {
                raw48 = (raw48 << 8) | quint64(data[i]);
            }
        } else {
            for (int i = 0; i < 6; i++) {
                raw48 |= quint64(data[i]) << (i * 8);
            }
        }
        qint64 v48s = (raw48 & (quint64(1) << 47)) ? qint64(raw48 | 0xFFFF000000000000ULL)
                                                   : qint64(raw48);
        quint64 v48u = raw48;
        // int64 / uint64
        quint64 raw64 = bigEndian ? qFromBigEndian<quint64>(data)
                                  : qFromLittleEndian<quint64>(data);
        qint64 v64s = *reinterpret_cast<const qint64 *>(&raw64);
        quint64 v64u = raw64;
        // float32
        quint32 rawf32 = bigEndian ? qFromBigEndian<quint32>(data)
                                   : qFromLittleEndian<quint32>(data);
        float vf32;
        memcpy(&vf32, &rawf32, sizeof(vf32));
        // double (float64)
        quint64 rawf64 = bigEndian ? qFromBigEndian<quint64>(data)
                                   : qFromLittleEndian<quint64>(data);
        double vf64;
        memcpy(&vf64, &rawf64, sizeof(vf64));
        // Update UI fields
        ui->v_int8->setText(QString::number(v8s));
        ui->v_uint8->setText(QString::number(v8u));
        ui->v_int16->setText(QString::number(v16s));
        ui->v_uint16->setText(QString::number(v16u));
        ui->v_int24->setText(QString::number(v24s));
        ui->v_uint24->setText(QString::number(v24u));
        ui->v_int32->setText(QString::number(v32s));
        ui->v_uint32->setText(QString::number(v32u));
        ui->v_int48->setText(QString::number(v48s));
        ui->v_uint48->setText(QString::number(v48u));
        ui->v_int64->setText(QString::number(v64s));
        ui->v_uint64->setText(QString::number(v64u));
        ui->v_float32->setText(QString::number(vf32, 'g', 8));
        ui->v_double->setText(QString::number(vf64, 'g', 16));
    } break;
    case 3: // Format
    {
        // Print formatted data using struct or inline spec
        QString structName = ui->formatStructComboBox->currentText().trimmed();
        QString inlineFmt = ui->formatInlineEdit->text().trimmed();
        QString out;
        if (!structName.isEmpty()) {
            out = Core()->cmdRawAt(QStringLiteral("%1").arg(structName), start_address).trimmed();
        } else if (!inlineFmt.isEmpty()) {
            if (inlineFmt.startsWith(".")) {
                out = Core()
                          ->cmdRawAt(QStringLiteral("pf%1").arg(inlineFmt), start_address)
                          .trimmed();
            } else {
                out = Core()
                          ->cmdRawAt(QStringLiteral("pf %1").arg(inlineFmt), start_address)
                          .trimmed();
            }
        }
        ui->formatOutput->setPlainText(out);
    } break;
    case 4: // Art (histogram/graph)
    {
        const int w = ui->histogram->width() / 12;
        // Build command based on combobox selection
        QString cmdBase = ui->comboBox->currentText().trimmed();
        char *s = r_str_newf("%s 4096@e:hex.addr=0@e:hex.cols=%d", qPrintable(cmdBase), w);
        ui->histogram->setText(cmd(s, at));
        free(s);
    } break;
    }
}

void HexdumpWidget::on_parseTypeComboBox_currentTextChanged(const QString &)
{
    QString currentParseTypeText = ui->parseTypeComboBox->currentData().toString();
    if (currentParseTypeText == "pda" || currentParseTypeText == "pci") {
        ui->hexSideFrame_2->show();
    } else {
        ui->hexSideFrame_2->hide();
    }
    refreshSelectionInfo();
}

void HexdumpWidget::on_parseEndianComboBox_currentTextChanged(const QString &)
{
    refreshSelectionInfo();
}

// React to changes in the value-endianness combo: update config and refresh displayed values
void HexdumpWidget::on_valueEndian_currentIndexChanged(int index)
{
    Core()->setConfig("cfg.bigendian", index == 1);
    refreshSelectionInfo();
}

void HexdumpWidget::on_hexSideTab_2_currentChanged(int /*index*/)
{
    /*
    if (index == 2) {
        // Add data to HTML Polar functions graph
        QFile html(":/html/bar.html");
        if(!html.open(QIODevice::ReadOnly)) {
            QMessageBox::information(0,"error",html.errorString());
        }
        QString code = html.readAll();
        html.close();
        this->histoWebView->setHtml(code);
        this->histoWebView->show();
    } else {
        this->histoWebView->hide();
    }
    */
}

void HexdumpWidget::resizeEvent(QResizeEvent *event)
{
    // Heuristics to hide sidebar when it hides the content of the hexdump.
    // 600px looks just "okay" Only applied when widget width is decreased to
    // avoid unwanted behavior
    if (event->oldSize().width() > event->size().width() && event->size().width() < 600) {
        showSidePanel(false);
    }
    QDockWidget::resizeEvent(event);
    refresh();
}

QWidget *HexdumpWidget::widgetToFocusOnRaise()
{
    return ui->hexTextView;
}

void HexdumpWidget::on_copyMD5_clicked()
{
    QString md5 = ui->bytesMD5->text();
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(md5);
    Core()->message("MD5 copied to clipboard: " + md5);
}

void HexdumpWidget::on_copySHA1_clicked()
{
    QString sha1 = ui->bytesSHA1->text();
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(sha1);
    Core()->message("SHA1 copied to clipboard: " + sha1);
}

void HexdumpWidget::on_copySHA256_clicked()
{
    QString sha256 = ui->bytesSHA256->text();
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(sha256);
    Core()->message("SHA256 copied to clipboard: " + sha256);
}

void HexdumpWidget::on_copyCRC32_clicked()
{
    QString crc32 = ui->bytesCRC32->text();
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(crc32);
    Core()->message("CRC32 copied to clipboard: " + crc32);
}

void HexdumpWidget::selectHexPreview()
{
    // Pre-select arch and bits in the hexdump sidebar
    QString arch = Core()->getConfig("asm.arch");
    QString bits = Core()->getConfig("asm.bits");

    if (ui->parseArchComboBox->findText(arch) != -1) {
        ui->parseArchComboBox->setCurrentIndex(ui->parseArchComboBox->findText(arch));
    }

    if (ui->parseBitsComboBox->findText(bits) != -1) {
        ui->parseBitsComboBox->setCurrentIndex(ui->parseBitsComboBox->findText(bits));
    }
}
