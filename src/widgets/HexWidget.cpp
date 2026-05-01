#include "HexWidget.h"
#include "Configuration.h"
#include "Iaito.h"
#include "common/Radare2Compat.h"
#include "common/ShortcutKeys.h"
#include "dialogs/FlagDialog.h"
#include "dialogs/ShortcutKeysDialog.h"
#include "dialogs/WriteCommandsDialogs.h"
#include <limits>
#include <set>
#include <QKeyEvent>
#include <QList>
#include <QStringList>

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QToolTip>
#include <QWheelEvent>
#include <QtEndian>

// Draw background highlighting for flags over the hex or ascii area
void HexWidget::drawFlagsBackground(QPainter &painter, bool ascii)
{
    updateFlagBackgroundRanges();
    if (flagBackgroundRanges.isEmpty()) {
        return;
    }

    painter.save();
    painter.setPen(Qt::NoPen);
    for (const auto &range : flagBackgroundRanges) {
        painter.setBrush(range.color);
        auto polys = rangePolygons(range.start, range.end, ascii);
        for (const auto &poly : polys) {
            painter.drawPolygon(poly);
        }
    }
    painter.restore();
}

void HexWidget::updateFlagBackgroundRanges()
{
    if (flagBackgroundRangesValid) {
        return;
    }

    flagBackgroundRangesValid = true;
    flagBackgroundRanges.clear();

    RFlag *rf = Core()->core()->flags;
    if (!rf || r_flag_count(rf, NULL) == 0) {
        return;
    }
    uint64_t startAddr = startAddress;
    uint64_t lastAddr = lastVisibleAddr();
    if (startAddr > lastAddr) {
        return;
    }

    std::set<RFlagItem *> drawn;
    for (uint64_t addr = startAddr;; ++addr) {
        RFlagItem *fi = r_flag_get_in(rf, addr);
        if (!fi || drawn.count(fi)) {
            if (addr == lastAddr) {
                break;
            }
            continue;
        }
        drawn.insert(fi);
        if (fi->size == 0) {
            if (addr == lastAddr) {
                break;
            }
            continue;
        }

        uint64_t start = fi->addr;
        uint64_t end = std::numeric_limits<uint64_t>::max();
        if (fi->size - 1 <= std::numeric_limits<uint64_t>::max() - fi->addr) {
            end = fi->addr + fi->size - 1;
        }

        RFlagItemMeta *fim = r_flag_get_meta(rf, fi->id);
        QColor bg = (fim && fim->color) ? QColor(QString::fromUtf8(fim->color)) : QColor(Qt::gray);
        bg.setAlphaF(0.3);
        flagBackgroundRanges.append({start, end, bg});
        if (addr == lastAddr) {
            break;
        }
    }
}
// Draw the status bar displaying offset and fd command output
void HexWidget::drawStatusBar(QPainter &painter)
{
    painter.save();
    // Ignore any scroll translation
    painter.resetTransform();

    // Prepare text metrics and position
    QFontMetrics fm = painter.fontMetrics();
    int x = 2;
    int y = viewport()->height() - 2;
    int textWidth = fm.horizontalAdvance(statusBarText);
    int textAscent = fm.ascent();
    int textDescent = fm.descent();
    int textHeight = textAscent + textDescent;
    QRect bgRect(x, y - textAscent, textWidth, textHeight);

    // Draw solid background behind status text
    painter.setPen(Qt::NoPen);
    painter.setBrush(backgroundColor);
    painter.drawRect(bgRect);

    // Draw status text on top
    painter.setPen(defColor);
    painter.drawText(x, y, statusBarText);
    painter.restore();
}

static constexpr uint64_t MAX_COPY_SIZE = 128 * 1024 * 1024;
static constexpr int MAX_LINE_WIDTH_PRESET = 32;
static constexpr int MAX_LINE_WIDTH_BYTES = 128 * 1024;

HexWidget::HexWidget(QWidget *parent)
    : QScrollArea(parent)
    , cursorEnabled(true)
    , cursorOnAscii(false)
    , updatingSelection(false)
    , itemByteLen(1)
    , itemGroupSize(1)
    , rowSizeBytes(16)
    , columnMode(ColumnMode::PowerOf2)
    , itemFormat(ItemFormatHex)
    , itemBigEndian(false)
    , addrCharLen(AddrWidth64)
    , showHeader(true)
    , showAscii(true)
    , showExHex(true)
    , showExAddr(true)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        viewport()->update();
    });

    connect(Config(), &Configuration::colorsUpdated, this, &HexWidget::updateColors);
    connect(Config(), &Configuration::fontsUpdated, this, [this]() {
        setMonospaceFont(Config()->getFont());
    });

    auto sizeActionGroup = new QActionGroup(this);
    for (int i = 1; i <= 8; i *= 2) {
        QAction *action = new QAction(QString::number(i), this);
        action->setCheckable(true);
        action->setActionGroup(sizeActionGroup);
        connect(action, &QAction::triggered, this, [=, this]() { setItemSize(i); });
        actionsItemSize.append(action);
    }
    actionsItemSize.at(0)->setChecked(true);

    /* Follow the order in ItemFormat enum */
    QStringList names;
    names << tr("Hexadecimal");
    names << tr("Octal");
    names << tr("Decimal");
    names << tr("Signed decimal");
    names << tr("Float");

    auto formatActionGroup = new QActionGroup(this);
    for (int i = 0; i < names.length(); ++i) {
        QAction *action = new QAction(names.at(i), this);
        action->setCheckable(true);
        action->setActionGroup(formatActionGroup);
        connect(action, &QAction::triggered, this, [=, this]() {
            setItemFormat(static_cast<ItemFormat>(i));
        });
        actionsItemFormat.append(action);
    }
    actionsItemFormat.at(0)->setChecked(true);
    actionsItemFormat.at(ItemFormatFloat)->setEnabled(false);

    rowSizeMenu = new QMenu(tr("Bytes per row"), this);
    auto columnsActionGroup = new QActionGroup(this);
    for (int i = 1; i <= MAX_LINE_WIDTH_PRESET; i *= 2) {
        QAction *action = new QAction(QString::number(i), rowSizeMenu);
        action->setCheckable(true);
        action->setActionGroup(columnsActionGroup);
        connect(action, &QAction::triggered, this, [=, this]() { setFixedLineSize(i); });
        rowSizeMenu->addAction(action);
    }
    rowSizeMenu->addSeparator();
    actionRowSizePowerOf2 = new QAction(tr("Power of 2"), this);
    actionRowSizePowerOf2->setCheckable(true);
    actionRowSizePowerOf2->setActionGroup(columnsActionGroup);
    connect(actionRowSizePowerOf2, &QAction::triggered, this, [this]() {
        setColumnMode(ColumnMode::PowerOf2);
    });
    rowSizeMenu->addAction(actionRowSizePowerOf2);

    actionItemBigEndian = new QAction(tr("Big Endian"), this);
    actionItemBigEndian->setCheckable(true);
    actionItemBigEndian->setEnabled(false);
    connect(actionItemBigEndian, &QAction::triggered, this, &HexWidget::setItemEndianess);

    actionHexPairs = new QAction(tr("Bytes as pairs"), this);
    actionHexPairs->setCheckable(true);
    connect(actionHexPairs, &QAction::triggered, this, &HexWidget::onHexPairsModeEnabled);

    actionCopy = new QAction(tr("Copy"), this);
    addAction(actionCopy);
    actionCopy->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);
    actionCopy->setShortcut(QKeySequence::Copy);
    connect(actionCopy, &QAction::triggered, this, &HexWidget::copy);

    actionCopyAddress = new QAction(tr("Copy address"), this);
    actionCopyAddress->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);
    actionCopyAddress->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_C);
    connect(actionCopyAddress, &QAction::triggered, this, &HexWidget::copyAddress);
    addAction(actionCopyAddress);
    // Action to copy selected bytes as a C string
    actionCopyAsCString = new QAction(tr("Copy as C string"), this);
    actionCopyAsCString->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);
    connect(actionCopyAsCString, &QAction::triggered, this, &HexWidget::copyAsCString);
    addAction(actionCopyAsCString);

    actionSelectRange = new QAction(tr("Select range..."), this);
    connect(actionSelectRange, &QAction::triggered, this, [this]() {
        rangeDialog.openAt(cursor.address);
    });
    addAction(actionSelectRange);
    connect(&rangeDialog, &QDialog::accepted, this, &HexWidget::onRangeDialogAccepted);

    actionsWriteString.reserve(5);
    actionWriteString = new QAction(tr("Write string..."), this);
    connect(actionWriteString, &QAction::triggered, this, &HexWidget::w_writeString);
    actionsWriteString.append(actionWriteString);

    actionWriteLenString = new QAction(tr("Write length and string..."), this);
    connect(actionWriteLenString, &QAction::triggered, this, &HexWidget::w_writePascalString);
    actionsWriteString.append(actionWriteLenString);

    actionWriteWideString = new QAction(tr("Write wide string..."), this);
    connect(actionWriteWideString, &QAction::triggered, this, &HexWidget::w_writeWideString);
    actionsWriteString.append(actionWriteWideString);

    actionWriteCString = new QAction(tr("Write zero terminated string..."), this);
    connect(actionWriteCString, &QAction::triggered, this, &HexWidget::w_writeCString);
    actionsWriteString.append(actionWriteCString);

    actionWriteBase64 = new QAction(tr("Write Base64 encoded/decoded string..."), this);
    connect(actionWriteBase64, &QAction::triggered, this, &HexWidget::w_write64);
    actionsWriteString.append(actionWriteBase64);

    actionsWriteNumber.reserve(4);
    struct NumAction
    {
        int bytes;
        const char *text;
    } numActions[] = {
        {1, "Write 8-bit value..."},
        {2, "Write 16-bit value..."},
        {4, "Write 32-bit value..."},
        {8, "Write 64-bit value..."},
    };
    for (auto &na : numActions) {
        QAction *act = new QAction(tr(na.text), this);
        connect(act, &QAction::triggered, this, [this, na] { writeNumber(na.bytes); });
        actionsWriteNumber.append(act);
    }

    // Setup address width selection actions (32-bit vs 64-bit)
    actionsAddressWidth.reserve(2);
    QActionGroup *addrWidthGroup = new QActionGroup(this);
    addrWidthGroup->setExclusive(true);
    // 32-bit addresses (8 hex digits)
    QAction *actionAddr32 = new QAction(tr("32-bit addresses"), this);
    actionAddr32->setCheckable(true);
    actionAddr32->setActionGroup(addrWidthGroup);
    connect(actionAddr32, &QAction::triggered, this, [this]() {
        addrCharLen = AddrWidth32;
        updateMetrics();
        refresh();
    });
    actionsAddressWidth.append(actionAddr32);
    // 64-bit addresses (16 hex digits)
    QAction *actionAddr64 = new QAction(tr("64-bit addresses"), this);
    actionAddr64->setCheckable(true);
    actionAddr64->setActionGroup(addrWidthGroup);
    connect(actionAddr64, &QAction::triggered, this, [this]() {
        addrCharLen = AddrWidth64;
        updateMetrics();
        refresh();
    });
    actionsAddressWidth.append(actionAddr64);
    // Set default checked based on initial addrCharLen
    if (addrCharLen == AddrWidth32) {
        actionAddr32->setChecked(true);
    } else {
        actionAddr64->setChecked(true);
    }
    actionWriteZeros = new QAction(tr("Write zeros..."), this);
    connect(actionWriteZeros, &QAction::triggered, this, &HexWidget::w_writeZeros);

    actionWriteRandom = new QAction(tr("Write random bytes..."), this);
    connect(actionWriteRandom, &QAction::triggered, this, &HexWidget::w_writeRandom);

    actionDuplicateFromOffset = new QAction(tr("Duplicate from offset..."), this);
    connect(actionDuplicateFromOffset, &QAction::triggered, this, &HexWidget::w_duplFromOffset);

    actionIncDec = new QAction(tr("Increment / Decrement..."), this);
    connect(actionIncDec, &QAction::triggered, this, &HexWidget::w_increaseDecrease);

    connect(this, &HexWidget::selectionChanged, this, [this](Selection selection) {
        actionCopy->setEnabled(!selection.empty);
    });

    updateMetrics();
    updateItemLength();

    startAddress = 0ULL;
    cursor.address = 0ULL;
    data.reset(new MemoryData());
    oldData.reset(new MemoryData());

    fetchData();
    updateCursorMeta();

    connect(&cursor.blinkTimer, &QTimer::timeout, this, &HexWidget::onCursorBlinked);
    cursor.setBlinkPeriod(1000);
    cursor.startBlinking();

    updateColors();
}

HexWidget::~HexWidget() {}

void HexWidget::setMonospaceFont(const QFont &font)
{
    if (!(font.styleHint() & QFont::Monospace)) {
        /* FIXME: Use default monospace font
        setFont(XXX); */
    }
    QScrollArea::setFont(font);
    monospaceFont = font.resolve(this->font());
    updateMetrics();
    fetchData();
    updateCursorMeta();

    viewport()->update();
}

void HexWidget::setItemSize(int nbytes)
{
    static const QVector<int> values({1, 2, 4, 8});

    if (!values.contains(nbytes))
        return;

    itemByteLen = nbytes;
    if (itemByteLen > rowSizeBytes) {
        rowSizeBytes = itemByteLen;
    }

    actionsItemFormat.at(ItemFormatFloat)->setEnabled(nbytes >= 4);
    actionItemBigEndian->setEnabled(nbytes != 1);

    updateItemLength();
    fetchData();
    updateCursorMeta();

    viewport()->update();
}

void HexWidget::setItemFormat(ItemFormat format)
{
    itemFormat = format;

    bool sizeEnabled = true;
    if (format == ItemFormatFloat)
        sizeEnabled = false;
    actionsItemSize.at(0)->setEnabled(sizeEnabled);
    actionsItemSize.at(1)->setEnabled(sizeEnabled);

    updateItemLength();
    fetchData();
    updateCursorMeta();

    viewport()->update();
}

void HexWidget::setItemGroupSize(int size)
{
    itemGroupSize = size;

    updateCounts();
    fetchData();
    updateCursorMeta();

    viewport()->update();
}

/**
 * @brief Checks if Item at the address changed compared to the last read data.
 * @param address Address of Item to be compared.
 * @return True if Item is different, False if Item is equal or last read didn't
 * contain the address.
 * @see HexWidget#readItem
 *
 * Checks if current Item at the address changed compared to the last read data.
 * It is assumed that the current read data buffer contains the address.
 */
bool HexWidget::isItemDifferentAt(uint64_t address)
{
    char oldItem[sizeof(uint64_t)] = {};
    char newItem[sizeof(uint64_t)] = {};
    if (data->copy(newItem, address, static_cast<size_t>(itemByteLen))
        && oldData->copy(oldItem, address, static_cast<size_t>(itemByteLen))) {
        return memcmp(oldItem, newItem, sizeof(oldItem)) != 0;
    }
    return false;
}

void HexWidget::updateCounts()
{
    actionHexPairs->setEnabled(
        rowSizeBytes > 1 && itemByteLen == 1 && itemFormat == ItemFormat::ItemFormatHex);
    actionHexPairs->setChecked(Core()->getConfigb("hex.pairs"));
    if (actionHexPairs->isChecked() && actionHexPairs->isEnabled()) {
        itemGroupSize = 2;
    } else {
        itemGroupSize = 1;
    }

    if (columnMode == ColumnMode::PowerOf2) {
        int last_good_size = itemGroupByteLen();
        for (int i = itemGroupByteLen(); i <= MAX_LINE_WIDTH_BYTES; i *= 2) {
            rowSizeBytes = i;
            itemColumns = rowSizeBytes / itemGroupByteLen();
            updateAreasPosition();
            if (horizontalScrollBar()->maximum() == 0) {
                last_good_size = rowSizeBytes;
            } else {
                break;
            }
        }
        rowSizeBytes = last_good_size;
    }

    itemColumns = rowSizeBytes / itemGroupByteLen();

    // ensure correct action is selected when changing line size
    // programmatically
    if (columnMode == ColumnMode::Fixed) {
        int w = 1;
        const auto &actions = rowSizeMenu->actions();
        for (auto action : actions) {
            action->setChecked(false);
        }
        for (auto action : actions) {
            if (w > MAX_LINE_WIDTH_PRESET) {
                break;
            }
            if (rowSizeBytes == w) {
                action->setChecked(true);
            }
            w *= 2;
        }
    } else if (columnMode == ColumnMode::PowerOf2) {
        actionRowSizePowerOf2->setChecked(true);
    }

    updateAreasPosition();
}

void HexWidget::setFixedLineSize(int lineSize)
{
    if (lineSize < 1 || lineSize < itemGroupByteLen() || lineSize % itemGroupByteLen()) {
        updateCounts();
        return;
    }
    rowSizeBytes = lineSize;
    columnMode = ColumnMode::Fixed;

    updateCounts();
    fetchData();
    updateCursorMeta();

    viewport()->update();
}

void HexWidget::setColumnMode(ColumnMode mode)
{
    columnMode = mode;

    updateCounts();
    fetchData();
    updateCursorMeta();

    viewport()->update();
}

void HexWidget::selectRange(RVA start, RVA end)
{
    BasicCursor endCursor(end);
    endCursor += 1;
    setCursorAddr(endCursor);
    selection.set(start, end);
    cursorEnabled = false;
    emit selectionChanged(getSelection());
}

void HexWidget::clearSelection()
{
    setCursorAddr(cursor.address, false);
    emit selectionChanged(getSelection());
}

HexWidget::Selection HexWidget::getSelection()
{
    return Selection{selection.isEmpty(), selection.start(), selection.end()};
}

void HexWidget::seek(uint64_t address)
{
    setCursorAddr(address);
}

void HexWidget::refresh()
{
    fetchData(true);
    viewport()->update();
}

void HexWidget::setItemEndianess(bool bigEndian)
{
    itemBigEndian = bigEndian;

    updateCursorMeta(); // Update cached item character

    viewport()->update();
}

void HexWidget::updateColors()
{
    borderColor = Config()->getColor("gui.border");
    backgroundColor = Config()->getColor("gui.background");
    b0x00Color = Config()->getColor("b0x00");
    b0x7fColor = Config()->getColor("b0x7f");
    b0xffColor = Config()->getColor("b0xff");
    printableColor = Config()->getColor("ai.write");
    defColor = Config()->getColor("btext");
    addrColor = Config()->getColor("func_var_addr");
    diffColor = Config()->getColor("graph.diff.unmatch");

    updateCursorMeta();
    viewport()->update();
}

void HexWidget::paintEvent(QPaintEvent *event)
{
    flagBackgroundRangesValid = false;

    QPainter painter(viewport());
    painter.setFont(monospaceFont);

    int xOffset = horizontalScrollBar()->value();
    if (xOffset > 0)
        painter.translate(QPoint(-xOffset, 0));

    if (event->rect() == cursor.screenPos.toAlignedRect()) {
        /* Cursor blink */
        drawCursor(painter);
        return;
    }

    painter.fillRect(event->rect().translated(xOffset, 0), backgroundColor);

    drawHeader(painter);

    drawAddrArea(painter);
    drawItemArea(painter);
    drawAsciiArea(painter);

    if (!cursorEnabled)
        return;

    drawCursor(painter, true);
    // Status bar is now handled by QStatusBar widget in the parent
}

void HexWidget::updateWidth()
{
    int max = (showAscii ? asciiArea.right() : itemArea.right()) - viewport()->width();
    if (max < 0)
        max = 0;
    else
        max += charWidth;
    horizontalScrollBar()->setMaximum(max);
    horizontalScrollBar()->setSingleStep(charWidth);
}

void HexWidget::resizeEvent(QResizeEvent *event)
{
    int oldByteCount = bytesPerScreen();
    updateCounts();

    if (event->oldSize().height() == event->size().height() && oldByteCount == bytesPerScreen())
        return;

    updateAreasHeight();
    fetchData(); // rowCount was changed
    updateCursorMeta();

    viewport()->update();
}

void HexWidget::mouseMoveEvent(QMouseEvent *event)
{
    QPoint pos = event->pos();
    pos.rx() += horizontalScrollBar()->value();

    auto mouseAddr = mousePosToAddr(pos).address;

    QString metaData = getFlagsAndComment(mouseAddr);
    if (!metaData.isEmpty() && (itemArea.contains(pos) || asciiArea.contains(pos))) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QToolTip::showText(event->globalPosition().toPoint(), metaData.replace(",", ", "), this);
#else
        QToolTip::showText(event->globalPos(), metaData.replace(",", ", "), this);
#endif
    } else {
        QToolTip::hideText();
    }

    if (!updatingSelection) {
        if (itemArea.contains(pos) || asciiArea.contains(pos))
            setCursor(Qt::IBeamCursor);
        else
            setCursor(Qt::ArrowCursor);
        return;
    }

    auto &area = currentArea();
    if (pos.x() < area.left())
        pos.setX(area.left());
    else if (pos.x() > area.right())
        pos.setX(area.right());
    auto addr = currentAreaPosToAddr(pos, true);
    setCursorAddr(addr, true);

    /* Stop blinking */
    cursorEnabled = false;

    viewport()->update();
}

void HexWidget::mousePressEvent(QMouseEvent *event)
{
    QPoint pos(event->pos());
    pos.rx() += horizontalScrollBar()->value();

    if (event->button() == Qt::LeftButton) {
        bool selectingData = itemArea.contains(pos);
        bool selecting = selectingData || asciiArea.contains(pos);
        if (selecting) {
            updatingSelection = true;
            setCursorOnAscii(!selectingData);
            auto cursorPosition = currentAreaPosToAddr(pos, true);
            setCursorAddr(cursorPosition, event->modifiers() == Qt::ShiftModifier);
            viewport()->update();
        }
    }
}

void HexWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (selection.isEmpty()) {
            selection.init(cursor.address);
            cursorEnabled = true;
        }
        updatingSelection = false;
    }
}

void HexWidget::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y();
        if (delta != 0) {
            qreal zoomFactor = Config()->getZoomFactor();
            zoomFactor += delta > 0 ? 0.1 : -0.1;
            Config()->setZoomFactor(zoomFactor);
        }
        event->accept();
        return;
    }

    // according to Qt doc 1 row per 5 degrees, angle measured in 1/8 of degree
    int dy = event->angleDelta().y() / (8 * 5);
    int64_t delta = -dy * itemRowByteLen();

    if (dy == 0)
        return;

    if (delta < 0 && startAddress < static_cast<uint64_t>(-delta)) {
        startAddress = 0;
    } else if (delta > 0 && data->maxIndex() < static_cast<uint64_t>(bytesPerScreen())) {
        startAddress = 0;
    } else {
        startAddress += delta;
    }
    fetchData();
    if (delta > 0
        && (data->maxIndex() - startAddress)
               <= static_cast<uint64_t>(bytesPerScreen() + delta - 1)) {
        startAddress = (data->maxIndex() - bytesPerScreen()) + 1;
    }
    if (cursor.address >= startAddress && cursor.address <= lastVisibleAddr()) {
        /* Don't enable cursor blinking if selection isn't empty */
        cursorEnabled = selection.isEmpty();
        updateCursorMeta();
    } else {
        cursorEnabled = false;
    }
    viewport()->update();
}

void HexWidget::keyPressEvent(QKeyEvent *event)
{
    // Handle Vim-like mark and jump
    if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_M) {
        ShortcutKeysDialog dlg(ShortcutKeysDialog::SetMark, cursor.address, this);
        dlg.exec();
        return;
    } else if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_Apostrophe) {
        ShortcutKeysDialog dlg(ShortcutKeysDialog::JumpTo, RVA_INVALID, this);
        if (dlg.exec() == QDialog::Accepted) {
            QChar key = dlg.selectedKey();
            RVA addr = ShortcutKeys::instance()->getMark(key);
            seek(addr);
        }
        return;
    }
    bool select = false;
    auto moveOrSelect =
        [event,
         &select](QKeySequence::StandardKey moveSeq, QKeySequence::StandardKey selectSeq) -> bool {
        if (event->matches(moveSeq)) {
            select = false;
            return true;
        } else if (event->matches(selectSeq)) {
            select = true;
            return true;
        }
        return false;
    };
    auto extendSel = [this, &select]() {
        if (select) {
            // Inclusive block-style extension: the cursor byte is always part of
            // the selection, so each shift+arrow step grows or shrinks by one cell.
            selection.updateInclusive(cursor.address);
            emit selectionChanged(getSelection());
            viewport()->update();
        }
    };
    if (moveOrSelect(QKeySequence::MoveToNextLine, QKeySequence::SelectNextLine)) {
        moveCursor(itemRowByteLen(), select);
        extendSel();
    } else if (moveOrSelect(QKeySequence::MoveToPreviousLine, QKeySequence::SelectPreviousLine)) {
        moveCursor(-itemRowByteLen(), select);
        extendSel();
    } else if (moveOrSelect(QKeySequence::MoveToNextChar, QKeySequence::SelectNextChar)) {
        moveCursor(cursorOnAscii ? 1 : itemByteLen, select);
        extendSel();
    } else if (moveOrSelect(QKeySequence::MoveToPreviousChar, QKeySequence::SelectPreviousChar)) {
        moveCursor(cursorOnAscii ? -1 : -itemByteLen, select);
        extendSel();
    } else if (moveOrSelect(QKeySequence::MoveToNextPage, QKeySequence::SelectNextPage)) {
        moveCursor(bytesPerScreen(), select);
        extendSel();
    } else if (moveOrSelect(QKeySequence::MoveToPreviousPage, QKeySequence::SelectPreviousPage)) {
        moveCursor(-bytesPerScreen(), select);
        extendSel();
    } else if (moveOrSelect(QKeySequence::MoveToStartOfLine, QKeySequence::SelectStartOfLine)) {
        int linePos = int((cursor.address % itemRowByteLen()) - (startAddress % itemRowByteLen()));
        moveCursor(-linePos, select);
        extendSel();
    } else if (moveOrSelect(QKeySequence::MoveToEndOfLine, QKeySequence::SelectEndOfLine)) {
        int linePos = int((cursor.address % itemRowByteLen()) - (startAddress % itemRowByteLen()));
        moveCursor(itemRowByteLen() - linePos, select);
        extendSel();
    }
    // viewport()->update();
}

HexWidget::HexContextInfo HexWidget::makeContextInfo(const QPoint &pt) const
{
    HexContextInfo ctx;
    ctx.localPoint = pt;
    ctx.clickedInHexArea = itemArea.contains(pt);
    ctx.clickedInAsciiArea = asciiArea.contains(pt);
    ctx.clickedAddress = mousePosToAddr(pt).address;
    ctx.hasSelection = !selection.isEmpty();
    ctx.clickedInsideSelection = ctx.hasSelection && selection.contains(ctx.clickedAddress);
    bool useSelection = ctx.hasSelection && ctx.clickedInsideSelection;
    ctx.effectiveAddress = useSelection ? selection.start() : ctx.clickedAddress;
    ctx.effectiveSize = useSelection ? selection.size() : 1;
    return ctx;
}

QIcon HexWidget::makeMenuIcon(MenuIcon icon, const QColor &color) const
{
    const qreal ratio = devicePixelRatioF();
    const int size = 16;
    QPixmap pixmap(size * ratio, size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor accent = color;
    accent.setAlpha(205);
    QPen pen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case MenuIcon::Copy:
        painter.drawRect(QRectF(5, 3.5, 7, 9));
        painter.drawRect(QRectF(3, 5.5, 7, 7));
        break;
    case MenuIcon::Insert:
        painter.drawLine(QPointF(8, 3.5), QPointF(8, 12.5));
        painter.drawLine(QPointF(3.5, 8), QPointF(12.5, 8));
        break;
    case MenuIcon::Edit:
        painter.drawLine(QPointF(4, 12), QPointF(12, 4));
        painter.drawLine(QPointF(10, 4), QPointF(12, 6));
        painter.drawLine(QPointF(4, 12), QPointF(7, 11));
        break;
    case MenuIcon::Selection:
        painter.drawLine(QPointF(3, 3), QPointF(6, 3));
        painter.drawLine(QPointF(3, 3), QPointF(3, 6));
        painter.drawLine(QPointF(13, 3), QPointF(10, 3));
        painter.drawLine(QPointF(13, 3), QPointF(13, 6));
        painter.drawLine(QPointF(3, 13), QPointF(6, 13));
        painter.drawLine(QPointF(3, 13), QPointF(3, 10));
        painter.drawLine(QPointF(13, 13), QPointF(10, 13));
        painter.drawLine(QPointF(13, 13), QPointF(13, 10));
        break;
    case MenuIcon::Flag:
        painter.drawLine(QPointF(4, 3), QPointF(4, 13));
        painter.drawPolyline(QPolygonF(
            {QPointF(4, 3), QPointF(12, 5), QPointF(8, 7), QPointF(12, 9), QPointF(4, 9)}));
        break;
    case MenuIcon::Analysis:
        painter.drawEllipse(QRectF(3, 3, 8, 8));
        painter.drawLine(QPointF(9.5, 9.5), QPointF(13, 13));
        break;
    case MenuIcon::View:
        painter.drawEllipse(QRectF(3, 5, 10, 6));
        painter.drawEllipse(QPointF(8, 8), 1.8, 1.8);
        break;
    case MenuIcon::Sync:
        painter.drawArc(QRectF(3, 3, 10, 10), 30 * 16, 240 * 16);
        painter.drawLine(QPointF(11, 4), QPointF(13, 5));
        painter.drawLine(QPointF(13, 5), QPointF(12, 7));
        break;
    case MenuIcon::Format:
        painter.drawText(QRectF(2, 2, 12, 12), Qt::AlignCenter, QStringLiteral("0x"));
        break;
    case MenuIcon::AddressWidth:
        painter.drawLine(QPointF(3, 8), QPointF(13, 8));
        painter.drawLine(QPointF(3, 6), QPointF(3, 10));
        painter.drawLine(QPointF(13, 6), QPointF(13, 10));
        break;
    case MenuIcon::BytesPerRow:
        painter.drawLine(QPointF(3, 5), QPointF(13, 5));
        painter.drawLine(QPointF(3, 8), QPointF(13, 8));
        painter.drawLine(QPointF(3, 11), QPointF(13, 11));
        break;
    case MenuIcon::Endian:
        painter.drawLine(QPointF(3, 8), QPointF(13, 8));
        painter.drawPolyline(QPolygonF({QPointF(5, 6), QPointF(3, 8), QPointF(5, 10)}));
        painter.drawPolyline(QPolygonF({QPointF(11, 6), QPointF(13, 8), QPointF(11, 10)}));
        break;
    case MenuIcon::Pairs:
        painter.drawText(QRectF(2, 2, 12, 12), Qt::AlignCenter, QStringLiteral("xx"));
        break;
    case MenuIcon::Number:
        painter.drawText(QRectF(2, 2, 12, 12), Qt::AlignCenter, QStringLiteral("42"));
        break;
    case MenuIcon::String:
        painter.drawText(QRectF(2, 2, 12, 12), Qt::AlignCenter, QStringLiteral("\""));
        break;
    case MenuIcon::Fill:
        painter.setBrush(accent);
        painter.drawRect(QRectF(3, 6, 10, 4));
        break;
    case MenuIcon::FollowIn:
        painter.drawPolyline(QPolygonF({QPointF(4, 8), QPointF(11, 8)}));
        painter.drawPolyline(QPolygonF({QPointF(8, 5), QPointF(11, 8), QPointF(8, 11)}));
        break;
    }

    return QIcon(pixmap);
}

void HexWidget::setActionIcon(QAction *action, MenuIcon icon, const QColor &color)
{
    if (action) {
        action->setIcon(makeMenuIcon(icon, color));
    }
}

void HexWidget::setMenuIcon(QMenu *menu, MenuIcon icon, const QColor &color)
{
    if (menu) {
        setActionIcon(menu->menuAction(), icon, color);
    }
}

QMenu *HexWidget::buildCopyMenu(QMenu *parent, const HexContextInfo &ctx)
{
    const QColor copyColor(56, 142, 60);
    QMenu *m = parent->addMenu(tr("Copy"));
    setMenuIcon(m, MenuIcon::Copy, copyColor);

    actionCopy->setText(tr("Copy bytes"));
    actionCopy->setEnabled(ctx.hasSelection);
    setActionIcon(actionCopy, MenuIcon::Copy, copyColor);
    m->addAction(actionCopy);

    setActionIcon(actionCopyAsCString, MenuIcon::String, copyColor);
    actionCopyAsCString->setEnabled(ctx.hasSelection);
    m->addAction(actionCopyAsCString);

    auto addPrintCopyAction = [this, m, ctx, copyColor](const QString &text, const QString &cmd) {
        QAction *action = m->addAction(text);
        setActionIcon(action, MenuIcon::String, copyColor);
        action->setEnabled(ctx.hasSelection);
        connect(action, &QAction::triggered, this, [this, cmd]() {
            if (selection.isEmpty()) {
                return;
            }
            const QString out = Core()->cmdRawAt(
                QStringLiteral("%1 %2").arg(cmd).arg(selection.size()), selection.start());
            QApplication::clipboard()->setText(out.trimmed());
        });
    };
    addPrintCopyAction(tr("Copy escaped string"), QStringLiteral("pcs"));
    addPrintCopyAction(tr("As shellscript"), QStringLiteral("pcS"));
    addPrintCopyAction(tr("For Python"), QStringLiteral("pcp"));
    addPrintCopyAction(tr("For GAS"), QStringLiteral("pca"));
    addPrintCopyAction(tr("For YARA"), QStringLiteral("pcy"));
    addPrintCopyAction(tr("As r2 script"), QStringLiteral("pc*"));

    m->addSeparator();
    setActionIcon(actionCopyAddress, MenuIcon::AddressWidth, copyColor);
    actionCopyAddress->setEnabled(true);
    m->addAction(actionCopyAddress);
    return m;
}

QMenu *HexWidget::buildInsertMenu(QMenu *parent)
{
    const QColor insertColor(245, 166, 35);
    QMenu *m = parent->addMenu(tr("Insert"));
    setMenuIcon(m, MenuIcon::Insert, insertColor);

    QMenu *stringMenu = m->addMenu(tr("String"));
    setMenuIcon(stringMenu, MenuIcon::String, insertColor);
    stringMenu->addActions(actionsWriteString);

    QMenu *valueMenu = m->addMenu(tr("Value"));
    setMenuIcon(valueMenu, MenuIcon::Number, insertColor);
    valueMenu->addActions(actionsWriteNumber);

    QMenu *fillMenu = m->addMenu(tr("Fill / Pattern"));
    setMenuIcon(fillMenu, MenuIcon::Fill, insertColor);
    fillMenu->addAction(actionWriteZeros);
    fillMenu->addAction(actionWriteRandom);

    m->addSeparator();
    setActionIcon(actionDuplicateFromOffset, MenuIcon::Edit, insertColor);
    m->addAction(actionDuplicateFromOffset);
    setActionIcon(actionIncDec, MenuIcon::Edit, insertColor);
    m->addAction(actionIncDec);
    return m;
}

void HexWidget::addFlagAction(QMenu *parent, const HexContextInfo &ctx)
{
    if (!ctx.clickedInHexArea && !ctx.clickedInAsciiArea) {
        return;
    }

    const QColor flagColor(245, 166, 35);
    const uint64_t flagAddr = ctx.effectiveAddress;
    const uint64_t defaultSize = ctx.effectiveSize;
    QAction *addFlag = parent->addAction(tr("Add flag..."));
    setActionIcon(addFlag, MenuIcon::Flag, flagColor);
    connect(addFlag, &QAction::triggered, this, [this, flagAddr, defaultSize]() {
        FlagDialog dlg(flagAddr, defaultSize, this);
        if (dlg.exec() == QDialog::Accepted) {
            refresh();
        }
    });
    parent->addSeparator();
}

QMenu *HexWidget::buildSelectionFlagsMenu(QMenu *parent, const HexContextInfo &ctx)
{
    const QColor selColor(142, 36, 170);
    const QColor flagColor(245, 166, 35);

    QMenu *m = parent->addMenu(tr("Select"));
    setMenuIcon(m, MenuIcon::Flag, selColor);

    setActionIcon(actionSelectRange, MenuIcon::Selection, selColor);
    m->addAction(actionSelectRange);

    if (ctx.hasSelection) {
        QAction *clearSel = m->addAction(tr("Clear selection"));
        setActionIcon(clearSel, MenuIcon::Selection, selColor);
        connect(clearSel, &QAction::triggered, this, &HexWidget::clearSelection);
    }

    if (ctx.clickedInHexArea || ctx.clickedInAsciiArea) {
        const uint64_t flagAddr = ctx.effectiveAddress;
        RFlagItem *fi = r_flag_get_in(Core()->core()->flags, flagAddr);
        if (fi) {
            m->addSeparator();
            QAction *editFlag = m->addAction(tr("Edit flag..."));
            setActionIcon(editFlag, MenuIcon::Flag, flagColor);
            connect(editFlag, &QAction::triggered, this, [this, flagAddr]() {
                FlagDialog dlg(flagAddr, 1, this);
                if (dlg.exec() == QDialog::Accepted) {
                    refresh();
                }
            });
        }
    }
    return m;
}

QMenu *HexWidget::buildViewFormatMenu(QMenu *parent)
{
    const QColor viewColor(0, 150, 136);
    const QColor formatColor(191, 128, 32);
    QMenu *m = parent->addMenu(tr("View"));
    setMenuIcon(m, MenuIcon::View, viewColor);

    QMenu *sizeMenu = m->addMenu(tr("Item size"));
    setMenuIcon(sizeMenu, MenuIcon::View, viewColor);
    sizeMenu->addActions(actionsItemSize);

    QMenu *formatMenu = m->addMenu(tr("Item format"));
    setMenuIcon(formatMenu, MenuIcon::Format, formatColor);
    formatMenu->addActions(actionsItemFormat);

    QMenu *addrWidthMenu = m->addMenu(tr("Address width"));
    setMenuIcon(addrWidthMenu, MenuIcon::AddressWidth, viewColor);
    addrWidthMenu->addActions(actionsAddressWidth);

    setMenuIcon(rowSizeMenu, MenuIcon::BytesPerRow, viewColor);
    m->addMenu(rowSizeMenu);

    m->addSeparator();
    setActionIcon(actionHexPairs, MenuIcon::Pairs, viewColor);
    m->addAction(actionHexPairs);
    setActionIcon(actionItemBigEndian, MenuIcon::Endian, viewColor);
    m->addAction(actionItemBigEndian);

    QStringList relVals = Core()->cmdList("e asm.addr.relto=?");
    relVals.removeAll(QString());
    if (!relVals.isEmpty()) {
        m->addSeparator();
        QString cur = Core()->getConfig("asm.addr.relto");
        QMenu *relMenu = m->addMenu(tr("Relative to"));
        setMenuIcon(relMenu, MenuIcon::View, formatColor);
        QActionGroup *grp = new QActionGroup(relMenu);
        grp->setExclusive(true);
        for (const QString &v : relVals) {
            QAction *act = relMenu->addAction(v);
            act->setCheckable(true);
            act->setActionGroup(grp);
            act->setChecked(v == cur);
        }
        connect(relMenu, &QMenu::triggered, this, [](QAction *act) {
            Core()->setConfig("asm.addr.relto", act->text());
            Core()->triggerRefreshAll();
        });
    }
    return m;
}

void HexWidget::addSyncOffsetActions(QMenu *parent)
{
    const QColor syncColor(216, 88, 64);

    QList<QAction *> external;
    const QList<QAction *> all = this->actions();
    QSet<QAction *> known{actionCopy, actionCopyAddress, actionCopyAsCString, actionSelectRange};
    for (QAction *a : all) {
        if (!known.contains(a)) {
            external.append(a);
        }
    }
    if (external.isEmpty()) {
        return;
    }

    parent->addSeparator();
    for (QAction *a : external) {
        setActionIcon(a, MenuIcon::Sync, syncColor);
        parent->addAction(a);
    }
}

void HexWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QPoint pt = event->pos();
    if (event->reason() == QContextMenuEvent::Mouse) {
        auto mouseAddr = mousePosToAddr(pt).address;
        if (asciiArea.contains(pt)) {
            cursorOnAscii = true;
        } else if (itemArea.contains(pt)) {
            cursorOnAscii = false;
        }
        if (selection.isEmpty()) {
            seek(mouseAddr);
        }
    }

    HexContextInfo ctx = makeContextInfo(pt);

    QMenu *menu = new QMenu();
    addFlagAction(menu, ctx);
    buildViewFormatMenu(menu);
    buildSelectionFlagsMenu(menu, ctx);
    buildCopyMenu(menu, ctx);
    buildInsertMenu(menu);
    addSyncOffsetActions(menu);

    menu->exec(mapToGlobal(pt));
    actionCopy->setEnabled(!selection.isEmpty());
    actionCopyAddress->setEnabled(true);
    actionCopyAsCString->setEnabled(true);
    menu->deleteLater();
}

void HexWidget::onCursorBlinked()
{
    if (!cursorEnabled)
        return;
    cursor.blink();
    QRect cursorRect = cursor.screenPos.toAlignedRect();
    viewport()->update(cursorRect.translated(-horizontalScrollBar()->value(), 0));
}

void HexWidget::onHexPairsModeEnabled(bool enable)
{
    // Sync configuration
    Core()->setConfig("hex.pairs", enable);
    if (enable) {
        setItemGroupSize(2);
    } else {
        setItemGroupSize(1);
    }
}

void HexWidget::copy()
{
    if (selection.isEmpty() || selection.size() > MAX_COPY_SIZE)
        return;

    QClipboard *clipboard = QApplication::clipboard();
    if (cursorOnAscii) {
        clipboard->setText(
            Core()
                ->cmdRawAt(QStringLiteral("psx %1").arg(selection.size()), selection.start())
                .trimmed());
    } else {
        clipboard->setText(
            Core()
                ->cmdRawAt(
                    QStringLiteral("p8 %1").arg(selection.size()),
                    selection.start())
                .trimmed()); // TODO: copy in the format shown
    }
}

void HexWidget::copyAddress()
{
    uint64_t addr = cursor.address;
    if (!selection.isEmpty()) {
        addr = selection.start();
    }
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(RAddressString(addr));
}

void HexWidget::onRangeDialogAccepted()
{
    if (rangeDialog.empty()) {
        seek(rangeDialog.getStartAddress());
        return;
    }
    selectRange(rangeDialog.getStartAddress(), rangeDialog.getEndAddress());
}

void HexWidget::copyAsCString()
{
    if (selection.isEmpty() || selection.size() > MAX_COPY_SIZE) {
        return;
    }
    uint64_t addr = selection.start();
    size_t len = selection.size();
    QByteArray data = Core()->ioRead(addr, int(len));
    QString out;
    out.reserve(len * 4 + 2);
    out.append('"');
    for (unsigned char c : data) {
        if (c == '\\') {
            out.append("\\\\");
        } else if (c == '"') {
            out.append("\\\"");
        } else if (c >= 0x20 && c <= 0x7e) {
            out.append(QChar(c));
        } else {
            out.append(QStringLiteral("\\x%1").arg((unsigned) c, 2, 16, QLatin1Char('0')).toUpper());
        }
    }
    out.append('"');
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(out);
}

void HexWidget::w_writeString()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    bool ok = false;
    QInputDialog d;
    d.setInputMode(QInputDialog::InputMode::TextInput);
    QString str = d.getText(this, tr("Write string"), tr("String:"), QLineEdit::Normal, "", &ok);
    if (ok && !str.isEmpty()) {
        Core()->cmdRawAt(QStringLiteral("w %1").arg(str), getLocationAddress());
        refresh();
    }
}

void HexWidget::w_increaseDecrease()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    IncrementDecrementDialog d;
    int ret = d.exec();
    if (ret == QDialog::Rejected) {
        return;
    }
    QString mode = d.getMode() == IncrementDecrementDialog::Increase ? "+" : "-";
    Core()->cmdRawAt(
        QStringLiteral("w%1%2 %3")
            .arg(QString::number(d.getNBytes()))
            .arg(mode)
            .arg(QString::number(d.getValue())),
        getLocationAddress());
    refresh();
}

void HexWidget::w_writeZeros()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    bool ok = false;
    QInputDialog d;

    int size = 1;
    if (!selection.isEmpty() && selection.size() <= INT_MAX) {
        size = static_cast<int>(selection.size());
    }

    QString str = QString::number(
        d.getInt(this, tr("Write zeros"), tr("Number of zeros:"), size, 1, 0x7FFFFFFF, 1, &ok));
    if (ok && !str.isEmpty()) {
        Core()->cmdRawAt(QStringLiteral("w0 %1").arg(str), getLocationAddress());
        refresh();
    }
}

void HexWidget::w_write64()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    Base64EnDecodedWriteDialog d;
    int ret = d.exec();
    if (ret == QDialog::Rejected) {
        return;
    }
    QString mode = d.getMode() == Base64EnDecodedWriteDialog::Encode ? "e" : "d";
    QByteArray str = d.getData();

    if (mode == "d"
        && (QString(str).contains(QRegularExpression("[^a-zA-Z0-9+/=]")) || str.length() % 4 != 0
            || str.isEmpty())) {
        QMessageBox::critical(
            this,
            tr("Error"),
            tr("Error occured during decoding your input.\n"
               "Please, make sure, that it is a valid base64 "
               "string and try again."));
        return;
    }

    Core()->cmdRawAt(
        QStringLiteral("w6%1 %2").arg(mode).arg(
            (mode == "e" ? str.toHex() : str).toStdString().c_str()),
        getLocationAddress());
    refresh();
}

void HexWidget::w_writeRandom()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    bool ok = false;
    QInputDialog d;

    int size = 1;
    if (!selection.isEmpty() && selection.size() <= INT_MAX) {
        size = static_cast<int>(selection.size());
    }
    QString nbytes = QString::number(
        d.getInt(this, tr("Write random"), tr("Number of bytes:"), size, 1, 0x7FFFFFFF, 1, &ok));
    if (ok && !nbytes.isEmpty()) {
        Core()->cmdRawAt(QStringLiteral("wr %1").arg(nbytes), getLocationAddress());
        refresh();
    }
}

void HexWidget::w_duplFromOffset()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    DuplicateFromOffsetDialog d;
    int ret = d.exec();
    if (ret == QDialog::Rejected) {
        return;
    }
    RVA copyFrom = d.getOffset();
    QString nBytes = QString::number(d.getNBytes());
    Core()->cmdRawAt(QStringLiteral("wd %1 %2").arg(copyFrom).arg(nBytes), getLocationAddress());
    refresh();
}

void HexWidget::w_writePascalString()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    bool ok = false;
    QInputDialog d;
    d.setInputMode(QInputDialog::InputMode::TextInput);
    QString str
        = d.getText(this, tr("Write Pascal string"), tr("String:"), QLineEdit::Normal, "", &ok);
    if (ok && !str.isEmpty()) {
        Core()->cmdRawAt(QStringLiteral("ws %1").arg(str), getLocationAddress());
        refresh();
    }
}

void HexWidget::w_writeWideString()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    bool ok = false;
    QInputDialog d;
    d.setInputMode(QInputDialog::InputMode::TextInput);
    QString str
        = d.getText(this, tr("Write wide string"), tr("String:"), QLineEdit::Normal, "", &ok);
    if (ok && !str.isEmpty()) {
        Core()->cmdRawAt(QStringLiteral("ww %1").arg(str), getLocationAddress());
        refresh();
    }
}

void HexWidget::w_writeCString()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    bool ok = false;
    QInputDialog d;
    d.setInputMode(QInputDialog::InputMode::TextInput);
    QString str = d.getText(
        this, tr("Write zero-terminated string"), tr("String:"), QLineEdit::Normal, "", &ok);
    if (ok && !str.isEmpty()) {
        Core()->cmdRawAt(QStringLiteral("wz %1").arg(str), getLocationAddress());
        refresh();
    }
}
// Prompt for a numeric expression and write it as byteCount-wide value at current location
void HexWidget::writeNumber(int byteCount)
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    bool ok = false;
    QString expr = QInputDialog::getText(
        this,
        tr("Write %1-bit value").arg(byteCount * 8),
        tr("Expression (supports flags/math):"),
        QLineEdit::Normal,
        QString(),
        &ok);
    if (!ok || expr.isEmpty()) {
        return;
    }
    ut64 value = Core()->math(expr);
    ut64 uValue;
    if (byteCount < 8) {
        ut64 mask = (1ULL << (byteCount * 8)) - 1;
        uValue = value & mask;
    } else {
        uValue = value;
    }
    QString hex = QString::number(uValue, 16).rightJustified(byteCount * 2, QLatin1Char('0'));
    bool bigEndian = Core()->getConfigb("cfg.bigendian");
    if (!bigEndian) {
        QString rev;
        for (int i = hex.length(); i > 0; i -= 2) {
            rev += hex.mid(i - 2, 2);
        }
        hex = rev;
    }
    Core()->cmdRawAt(QStringLiteral("wx %1").arg(hex), getLocationAddress());
    refresh();
}

void HexWidget::updateItemLength()
{
    itemPrefixLen = 0;

    switch (itemFormat) {
    case ItemFormatHex:
        itemCharLen = 2 * itemByteLen;
        if (itemByteLen > 1 && showExHex)
            itemPrefixLen = hexPrefix.length();
        break;
    case ItemFormatOct:
        itemCharLen = (itemByteLen * 8 + 3) / 3;
        break;
    case ItemFormatDec:
        switch (itemByteLen) {
        case 1:
            itemCharLen = 3;
            break;
        case 2:
            itemCharLen = 5;
            break;
        case 4:
            itemCharLen = 10;
            break;
        case 8:
            itemCharLen = 20;
            break;
        }
        break;
    case ItemFormatSignedDec:
        switch (itemByteLen) {
        case 1:
            itemCharLen = 4;
            break;
        case 2:
            itemCharLen = 6;
            break;
        case 4:
            itemCharLen = 11;
            break;
        case 8:
            itemCharLen = 20;
            break;
        }
        break;
    case ItemFormatFloat:
        if (itemByteLen < 4)
            itemByteLen = 4;
        // FIXME
        itemCharLen = 3 * itemByteLen;
        break;
    }

    itemCharLen += itemPrefixLen;

    updateCounts();
}

void HexWidget::drawHeader(QPainter &painter)
{
    if (!showHeader)
        return;

    int offset = 0;
    QRectF rect(itemArea.left(), 0, itemWidth(), lineHeight);

    painter.setPen(addrColor);

    for (int j = 0; j < itemColumns; ++j) {
        for (int k = 0; k < itemGroupSize; ++k, offset += itemByteLen) {
            painter.drawText(
                rect, Qt::AlignVCenter | Qt::AlignRight, QString::number(offset, 16).toUpper());
            rect.translate(itemWidth(), 0);
        }
        rect.translate(columnSpacingWidth(), 0);
    }

    rect.moveLeft(asciiArea.left());
    rect.setWidth(charWidth);
    for (int j = 0; j < itemRowByteLen(); ++j) {
        painter
            .drawText(rect, Qt::AlignVCenter | Qt::AlignRight, QString::number(j % 16, 16).toUpper());
        rect.translate(charWidth, 0);
    }
}

void HexWidget::drawCursor(QPainter &painter, bool shadow)
{
    if (shadow) {
        shadowCursor.screenPos.setWidth(cursorOnAscii ? itemWidth() : charWidth);
        QColor hi = palette().color(QPalette::Highlight);
        QColor fill = hi;
        fill.setAlpha(72);
        painter.fillRect(shadowCursor.screenPos, fill);
        QPen pen(hi);
        pen.setStyle(Qt::SolidLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(shadowCursor.screenPos);
    }

    painter.setPen(cursor.cachedColor);
    QRectF charRect(cursor.screenPos);
    charRect.setWidth(charWidth);
    painter.fillRect(charRect, backgroundColor);
    painter.drawText(charRect, Qt::AlignVCenter, cursor.cachedChar);
    if (cursor.isVisible) {
        painter.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
        painter.fillRect(cursor.screenPos, QColor(0xff, 0xff, 0xff));
    }
}

void HexWidget::drawAddrArea(QPainter &painter)
{
    uint64_t offset = startAddress;
    QString addrString;
    QSizeF areaSize((addrCharLen + (showExAddr ? 2 : 0)) * charWidth, lineHeight);
    QRectF strRect(addrArea.topLeft(), areaSize);

    const QColor rowHi = palette().color(QPalette::Highlight);
    const int rowLen = itemRowByteLen();
    for (int line = 0; line < visibleLines && offset <= data->maxIndex();
         ++line, strRect.translate(0, lineHeight), offset += rowLen) {
        addrString = QStringLiteral("%1").arg(offset, addrCharLen, 16, QLatin1Char('0'));
        if (showExAddr)
            addrString.prepend(hexPrefix);
        const bool rowHasSelection = !selection.isEmpty()
                                     && selection.intersects(offset, offset + rowLen - 1);
        if (rowHasSelection) {
            QColor rowBg = rowHi;
            rowBg.setAlpha(72);
            painter.fillRect(strRect, rowBg);
            painter.setPen(rowHi);
        } else {
            painter.setPen(addrColor);
        }
        painter.drawText(strRect, Qt::AlignVCenter, addrString);
    }

    painter.setPen(borderColor);

    qreal vLineOffset = itemArea.left() - charWidth;
    painter.drawLine(QLineF(vLineOffset, 0, vLineOffset, viewport()->height()));
}

void HexWidget::drawItemArea(QPainter &painter)
{
    QRectF itemRect(itemArea.topLeft(), QSizeF(itemWidth(), lineHeight));
    QColor itemColor;
    QString itemString;

    // Draw flag-backed highlights before any selection
    drawFlagsBackground(painter, false);
    fillSelectionBackground(painter);

    uint64_t itemAddr = startAddress;
    for (int line = 0; line < visibleLines; ++line) {
        itemRect.moveLeft(itemArea.left());
        for (int j = 0; j < itemColumns; ++j) {
            for (int k = 0; k < itemGroupSize && itemAddr <= data->maxIndex();
                 ++k, itemAddr += itemByteLen) {
                itemString = renderItem(itemAddr - startAddress, &itemColor);
                if (selection.contains(itemAddr)) {
                    itemColor = palette().highlightedText().color();
                }
                if (isItemDifferentAt(itemAddr)) {
                    itemColor.setRgb(diffColor.rgb());
                }
                painter.setPen(itemColor);
                painter.drawText(itemRect, Qt::AlignVCenter, itemString);
                itemRect.translate(itemWidth(), 0);
                if (cursor.address == itemAddr) {
                    auto &itemCursor = cursorOnAscii ? shadowCursor : cursor;
                    itemCursor.cachedChar = itemString.at(0);
                    itemCursor.cachedColor = itemColor;
                }
            }
            itemRect.translate(columnSpacingWidth(), 0);
        }
        itemRect.translate(0, lineHeight);
    }

    painter.setPen(borderColor);

    qreal vLineOffset = asciiArea.left() - charWidth;
    painter.drawLine(QLineF(vLineOffset, 0, vLineOffset, viewport()->height()));
}

void HexWidget::drawAsciiArea(QPainter &painter)
{
    QRectF charRect(asciiArea.topLeft(), QSizeF(charWidth, lineHeight));

    // Draw flag-backed highlights before any selection in ASCII area
    drawFlagsBackground(painter, true);
    fillSelectionBackground(painter, true);

    uint64_t address = startAddress;
    QChar ascii;
    QColor color;
    for (int line = 0; line < visibleLines; ++line, charRect.translate(0, lineHeight)) {
        charRect.moveLeft(asciiArea.left());
        for (int j = 0; j < itemRowByteLen() && address <= data->maxIndex(); ++j, ++address) {
            ascii = renderAscii(address - startAddress, &color);
            if (selection.contains(address)) {
                color = palette().highlightedText().color();
            }
            if (isItemDifferentAt(address)) {
                color.setRgb(diffColor.rgb());
            }
            painter.setPen(color);
            /* Dots look ugly. Use fillRect() instead of drawText(). */
            if (ascii == '.') {
                qreal a = cursor.screenPos.width();
                QPointF p = charRect.bottomLeft();
                p.rx() += (charWidth - a) / 2 + 1;
                p.ry() += -2 * a;
                painter.fillRect(QRectF(p, QSizeF(a, a)), color);
            } else {
                painter.drawText(charRect, Qt::AlignVCenter, ascii);
            }
            charRect.translate(charWidth, 0);
            if (cursor.address == address) {
                auto &itemCursor = cursorOnAscii ? cursor : shadowCursor;
                itemCursor.cachedChar = ascii;
                itemCursor.cachedColor = color;
            }
        }
    }
}

void HexWidget::fillSelectionBackground(QPainter &painter, bool ascii)
{
    if (selection.isEmpty()) {
        return;
    }
    const auto parts = rangePolygons(selection.start(), selection.end(), ascii);
    const QColor highlightColor = palette().color(QPalette::Highlight);
    painter.setBrush(highlightColor);
    painter.setPen(Qt::NoPen);
    for (const auto &shape : parts) {
        painter.drawPolygon(shape);
    }
}

QVector<QPolygonF> HexWidget::rangePolygons(RVA start, RVA last, bool ascii)
{
    if (last < startAddress || start > lastVisibleAddr()) {
        return {};
    }

    QRectF rect;
    const QRectF area = QRectF(ascii ? asciiArea : itemArea);

    /* Convert absolute values to relative */
    int startOffset = std::max(uint64_t(start), startAddress) - startAddress;
    int endOffset = std::min(uint64_t(last), lastVisibleAddr()) - startAddress;

    QVector<QPolygonF> parts;

    auto getRectangle = [&](int offset) {
        return QRectF(ascii ? asciiRectangle(offset) : itemRectangle(offset));
    };

    auto startRect = getRectangle(startOffset);
    auto endRect = getRectangle(endOffset);
    if (!ascii) {
        if (int startFraction = startOffset % itemByteLen) {
            startRect.setLeft(startRect.left() + startFraction * startRect.width() / itemByteLen);
        }
        if (int endFraction = itemByteLen - 1 - (endOffset % itemByteLen)) {
            endRect.setRight(endRect.right() - endFraction * endRect.width() / itemByteLen);
        }
    }
    if (endOffset - startOffset + 1 <= rowSizeBytes) {
        if (startOffset / rowSizeBytes == endOffset / rowSizeBytes) { // single row
            rect = startRect;
            rect.setRight(endRect.right());
            parts.push_back(QPolygonF(rect));
        } else {
            // two separate rectangles
            rect = startRect;
            rect.setRight(area.right());
            parts.push_back(QPolygonF(rect));
            rect = endRect;
            rect.setLeft(area.left());
            parts.push_back(QPolygonF(rect));
        }
    } else {
        // single multiline shape
        QPolygonF shape;
        shape << startRect.topLeft();
        rect = getRectangle(startOffset + rowSizeBytes - 1 - startOffset % rowSizeBytes);
        shape << rect.topRight();
        if (endOffset % rowSizeBytes != rowSizeBytes - 1) {
            rect = getRectangle(endOffset - endOffset % rowSizeBytes - 1);
            shape << rect.bottomRight() << endRect.topRight();
        }
        shape << endRect.bottomRight();
        shape << getRectangle(endOffset - endOffset % rowSizeBytes).bottomLeft();
        if (startOffset % rowSizeBytes) {
            rect = getRectangle(startOffset - startOffset % rowSizeBytes + rowSizeBytes);
            shape << rect.topLeft() << startRect.bottomLeft();
        }
        shape << shape.first(); // close the shape
        parts.push_back(shape);
    }
    return parts;
}

void HexWidget::updateMetrics()
{
    QFontMetricsF fontMetrics(this->monospaceFont);
    lineHeight = fontMetrics.height();
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    charWidth = fontMetrics.horizontalAdvance(QLatin1Char('F'));
#else
    charWidth = fontMetrics.width(QLatin1Char('F'));
#endif

    updateCounts();
    updateAreasHeight();

    cursor.screenPos.setHeight(lineHeight);
    shadowCursor.screenPos.setHeight(lineHeight);

    // Active cursor spans one character (a nibble in hex, a char in ASCII)
    cursor.screenPos.setWidth(charWidth);
    if (cursorOnAscii) {
        cursor.screenPos.moveTopLeft(asciiArea.topLeft());
        shadowCursor.screenPos.setWidth(itemWidth());
        shadowCursor.screenPos.moveTopLeft(itemArea.topLeft());
    } else {
        cursor.screenPos.moveTopLeft(itemArea.topLeft());
        shadowCursor.screenPos.setWidth(charWidth);
        shadowCursor.screenPos.moveTopLeft(asciiArea.topLeft());
    }
}

void HexWidget::updateAreasPosition()
{
    const qreal spacingWidth = areaSpacingWidth();

    qreal yOffset = showHeader ? lineHeight : 0;

    addrArea.setTopLeft(QPointF(0, yOffset));
    addrArea.setWidth((addrCharLen + (showExAddr ? 2 : 0)) * charWidth);

    itemArea.setTopLeft(QPointF(addrArea.right() + spacingWidth, yOffset));
    itemArea.setWidth(itemRowWidth());

    asciiArea.setTopLeft(QPointF(itemArea.right() + spacingWidth, yOffset));
    asciiArea.setWidth(asciiRowWidth());

    updateWidth();
}

void HexWidget::updateAreasHeight()
{
    visibleLines = static_cast<int>((viewport()->height() - itemArea.top()) / lineHeight);

    qreal height = visibleLines * lineHeight;
    addrArea.setHeight(height);
    itemArea.setHeight(height);
    asciiArea.setHeight(height);
}

void HexWidget::moveCursor(int offset, bool select)
{
    BasicCursor addr = cursor.address;
    addr += offset;
    if (addr.address > data->maxIndex()) {
        addr.address = data->maxIndex();
    }
    setCursorAddr(addr, select);
}

void HexWidget::setCursorAddr(BasicCursor addr, bool select)
{
    bool addressChanged = cursor.address != addr.address;

    if (!select) {
        bool clearingSelection = !selection.isEmpty();
        selection.init(addr);
        if (clearingSelection)
            emit selectionChanged(getSelection());
    }
    if (addressChanged) {
        emit positionChanged(addr.address);
    }

    cursor.address = addr.address;
    statusBarText = RAddressString(cursor.address);

    /* Pause cursor repainting */
    cursorEnabled = false;

    if (select) {
        selection.update(addr);
        emit selectionChanged(getSelection());
    }

    uint64_t addressValue = cursor.address;
    /* Update data cache if necessary */
    if (!(addressValue >= startAddress && addressValue <= lastVisibleAddr())) {
        /* Align start address */
        addressValue -= (addressValue % itemRowByteLen());

        /* FIXME: handling Page Up/Down */
        if (addressValue == startAddress + bytesPerScreen()) {
            startAddress += itemRowByteLen();
        } else {
            startAddress = addressValue;
        }

        fetchData();

        if (startAddress > (data->maxIndex() - bytesPerScreen()) + 1) {
            startAddress = (data->maxIndex() - bytesPerScreen()) + 1;
        }
    }

    updateCursorMeta();

    /* Draw cursor */
    cursor.isVisible = !select;
    viewport()->update();

    /* Resume cursor repainting */
    cursorEnabled = selection.isEmpty();
}

void HexWidget::updateCursorMeta()
{
    QPointF point;
    QPointF pointAscii;

    int offset = cursor.address - startAddress;
    int itemOffset = offset;
    int asciiOffset;

    /* Calc common Y coordinate */
    point.ry() = (itemOffset / itemRowByteLen()) * lineHeight;
    pointAscii.setY(point.y());
    itemOffset %= itemRowByteLen();
    asciiOffset = itemOffset;

    /* Calc X coordinate on the item area */
    point.rx() = (itemOffset / itemGroupByteLen()) * columnExWidth();
    itemOffset %= itemGroupByteLen();
    point.rx() += (itemOffset / itemByteLen) * itemWidth();

    /* Calc X coordinate on the ascii area */
    pointAscii.rx() = asciiOffset * charWidth;

    point += itemArea.topLeft();
    pointAscii += asciiArea.topLeft();

    cursor.screenPos.moveTopLeft(cursorOnAscii ? pointAscii : point);
    shadowCursor.screenPos.moveTopLeft(cursorOnAscii ? point : pointAscii);
}

void HexWidget::setCursorOnAscii(bool ascii)
{
    cursorOnAscii = ascii;
}

const QColor HexWidget::itemColor(uint8_t byte)
{
    QColor color(defColor);

    if (byte == 0x00)
        color = b0x00Color;
    else if (byte == 0x7f)
        color = b0x7fColor;
    else if (byte == 0xff)
        color = b0xffColor;
    else if (IS_PRINTABLE(byte)) {
        color = printableColor;
    }

    return color;
}

template<class T>
static T fromBigEndian(const void *src)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    return qFromBigEndian<T>(src);
#else
    T result;
    memcpy(&result, src, sizeof(T));
    return qFromBigEndian<T>(result);
#endif
}

template<class T>
static T fromLittleEndian(const void *src)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    return qFromLittleEndian<T>(src);
#else
    T result;
    memcpy(&result, src, sizeof(T));
    return qFromLittleEndian<T>(result);
#endif
}

QVariant HexWidget::readItem(int offset, QColor *color)
{
    quint8 byte;
    quint16 word;
    quint32 dword;
    quint64 qword;
    float float32;
    double float64;

    quint8 bytes[sizeof(uint64_t)];
    data->copy(bytes, startAddress + offset, static_cast<size_t>(itemByteLen));
    const bool signedItem = itemFormat == ItemFormatSignedDec;

    if (color) {
        *color = defColor;
    }

    switch (itemByteLen) {
    case 1:
        byte = bytes[0];
        if (color)
            *color = itemColor(byte);
        if (!signedItem)
            return QVariant(static_cast<quint64>(byte));
        return QVariant(static_cast<qint64>(static_cast<qint8>(byte)));
    case 2:
        if (itemBigEndian)
            word = fromBigEndian<quint16>(bytes);
        else
            word = fromLittleEndian<quint16>(bytes);

        if (!signedItem)
            return QVariant(static_cast<quint64>(word));
        return QVariant(static_cast<qint64>(static_cast<qint16>(word)));
    case 4:
        if (itemBigEndian)
            dword = fromBigEndian<quint32>(bytes);
        else
            dword = fromLittleEndian<quint32>(bytes);

        if (itemFormat == ItemFormatFloat) {
            memcpy(&float32, &dword, sizeof(float32));
            return QVariant(float32);
        }
        if (!signedItem)
            return QVariant(static_cast<quint64>(dword));
        return QVariant(static_cast<qint64>(static_cast<qint32>(dword)));
    case 8:
        if (itemBigEndian)
            qword = fromBigEndian<quint64>(bytes);
        else
            qword = fromLittleEndian<quint64>(bytes);
        if (itemFormat == ItemFormatFloat) {
            memcpy(&float64, &qword, sizeof(float64));
            return QVariant(float64);
        }
        if (!signedItem)
            return QVariant(qword);
        return QVariant(static_cast<qint64>(qword));
    }

    return QVariant();
}

QString HexWidget::renderItem(int offset, QColor *color)
{
    QString item;
    QVariant itemVal = readItem(offset, color);
    int itemLen = itemCharLen - itemPrefixLen; /* Reserve space for prefix */

    // FIXME: handle broken itemVal ( QVariant() )
    switch (itemFormat) {
    case ItemFormatHex:
        item = QStringLiteral("%1").arg(itemVal.toULongLong(), itemLen, 16, QLatin1Char('0'));
        if (itemByteLen > 1 && showExHex)
            item.prepend(hexPrefix);
        break;
    case ItemFormatOct:
        item = QStringLiteral("%1").arg(itemVal.toULongLong(), itemLen, 8, QLatin1Char('0'));
        break;
    case ItemFormatDec:
        item = QStringLiteral("%1").arg(itemVal.toULongLong(), itemLen, 10);
        break;
    case ItemFormatSignedDec:
        item = QStringLiteral("%1").arg(itemVal.toLongLong(), itemLen, 10);
        break;
    case ItemFormatFloat:
        item = QStringLiteral("%1").arg(itemVal.toDouble(), itemLen);
        break;
    }

    return item;
}

QChar HexWidget::renderAscii(int offset, QColor *color)
{
    uchar byte;
    data->copy(&byte, startAddress + offset, sizeof(byte));
    if (color) {
        *color = itemColor(byte);
    }
    if (!IS_PRINTABLE(byte)) {
        byte = '_';
    }
    return QChar(byte);
}

/**
 * @brief Gets the available flags and comment at a specific address.
 * @param address Address of Item to be checked.
 * @return String containing the flags and comment available at the address.
 */
QString HexWidget::getFlagsAndComment(uint64_t address)
{
    QString metaData;
    // Gather detailed flag info: name, size, color, comment
    // Show flag covering this address (if any)
    RFlag *rf = Core()->core()->flags;
    if (rf) {
        RFlagItem *fi = r_flag_get_in(rf, address);
        if (fi) {
            // Build single entry for the flag
            QString entry = QString::fromUtf8(fi->name);
            entry += QString(" (size: %1").arg(fi->size);
            RFlagItemMeta *fim = r_flag_get_meta(rf, fi->id);
            if (fim && fim->color) {
                entry += QString(", color: %1").arg(QString::fromUtf8(fim->color));
            }
            entry += ")";
            if (fim && fim->comment) {
                entry += QString(" [comment: %1]").arg(QString::fromUtf8(fim->comment).trimmed());
            }
            metaData = QStringLiteral("Flag:\n") + entry;
        }
    }
    // Append general comment if present
    QString comment = Core()->getCommentAt(address);
    if (!comment.isEmpty()) {
        if (!metaData.isEmpty()) {
            metaData.append("\n");
        }
        metaData.append(QStringLiteral("Comment: %1").arg(comment.trimmed()));
    }
    return metaData;
}

bool HexWidget::isDataAvailable(uint64_t address, int length)
{
    if (!data || length <= 0) {
        return false;
    }

    const uint64_t minAddress = data->minIndex();
    const uint64_t maxAddress = data->maxIndex();
    if (maxAddress < minAddress || address < minAddress) {
        return false;
    }

    const uint64_t lengthValue = static_cast<uint64_t>(length);
    if (lengthValue - 1 > std::numeric_limits<uint64_t>::max() - address) {
        return false;
    }

    return address + lengthValue - 1 <= maxAddress;
}

void HexWidget::fetchData(bool force)
{
    if (!force && isDataAvailable(startAddress, bytesPerScreen())) {
        return;
    }

    data.swap(oldData);
    data->fetch(startAddress, bytesPerScreen());
}

BasicCursor HexWidget::screenPosToAddr(const QPoint &point, bool middle) const
{
    QPointF pt = point - itemArea.topLeft();

    int relativeAddress = 0;
    int line = static_cast<int>(pt.y() / lineHeight);
    relativeAddress += line * itemRowByteLen();
    int column = static_cast<int>(pt.x() / columnExWidth());
    relativeAddress += column * itemGroupByteLen();
    pt.rx() -= column * columnExWidth();
    auto roundingOffset = middle ? itemWidth() / 2 : 0;
    relativeAddress += static_cast<int>((pt.x() + roundingOffset) / itemWidth()) * itemByteLen;
    BasicCursor result(startAddress);
    result += relativeAddress;
    return result;
}

BasicCursor HexWidget::asciiPosToAddr(const QPoint &point, bool middle) const
{
    QPointF pt = point - asciiArea.topLeft();

    int relativeAddress = 0;
    relativeAddress += static_cast<int>(pt.y() / lineHeight) * itemRowByteLen();
    auto roundingOffset = middle ? (charWidth / 2) : 0;
    relativeAddress += static_cast<int>((pt.x() + (roundingOffset)) / charWidth);
    BasicCursor result(startAddress);
    result += relativeAddress;
    return result;
}

BasicCursor HexWidget::currentAreaPosToAddr(const QPoint &point, bool middle) const
{
    return cursorOnAscii ? asciiPosToAddr(point, middle) : screenPosToAddr(point, middle);
}

BasicCursor HexWidget::mousePosToAddr(const QPoint &point, bool middle) const
{
    return asciiArea.contains(point) ? asciiPosToAddr(point, middle)
                                     : screenPosToAddr(point, middle);
}

QRectF HexWidget::itemRectangle(int offset)
{
    qreal x;
    qreal y;

    qreal width = itemWidth();
    y = (offset / itemRowByteLen()) * lineHeight;
    offset %= itemRowByteLen();

    x = (offset / itemGroupByteLen()) * columnExWidth();
    offset %= itemGroupByteLen();
    x += (offset / itemByteLen) * itemWidth();
    if (offset == 0) {
        x -= charWidth / 2;
        width += charWidth / 2;
    }
    if (static_cast<int>(offset) == itemGroupByteLen() - 1) {
        width += charWidth / 2;
    }

    x += itemArea.x();
    y += itemArea.y();

    return QRectF(x, y, width, lineHeight);
}

QRectF HexWidget::asciiRectangle(int offset)
{
    QPointF p;

    p.ry() = (offset / itemRowByteLen()) * lineHeight;
    offset %= itemRowByteLen();

    p.rx() = offset * charWidth;

    p += asciiArea.topLeft();

    return QRectF(p, QSizeF(charWidth, lineHeight));
}

RVA HexWidget::getLocationAddress()
{
    return !selection.isEmpty() ? selection.start() : cursor.address;
}
