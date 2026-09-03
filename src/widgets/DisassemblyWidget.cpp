#include "DisassemblyWidget.h"
#include "common/Configuration.h"
#include "common/DeepLink.h"
#include "common/Helpers.h"
#include "common/PreviewTooltip.h"
#include "common/SelectionHighlight.h"
#include "common/ShortcutKeys.h"
#include "common/ShortcutManager.h"
#include "common/TempConfig.h"
#include "core/MainWindow.h"
#include "dialogs/ShortcutKeysDialog.h"
#include "menus/DisassemblyContextMenu.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBlockUserData>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

class DisassemblyTextBlockUserData : public QTextBlockUserData
{
public:
    DisassemblyLine line;

    explicit DisassemblyTextBlockUserData(const DisassemblyLine &line) { this->line = line; }
};

static DisassemblyTextBlockUserData *getUserData(const QTextBlock &block)
{
    QTextBlockUserData *userData = block.userData();
    if (!userData) {
        return nullptr;
    }

    return static_cast<DisassemblyTextBlockUserData *>(userData);
}

static int getMaxVisibleDisassemblyLines(QPlainTextEdit *textEdit)
{
    if (!textEdit || textEdit->viewport()->height() <= 0) {
        return 0;
    }

    QTextDocument *document = textEdit->document();
    const QFontMetricsF fm(document->defaultFont());
    const qreal lineHeight = qMax<qreal>(1.0, fm.lineSpacing());
    const qreal availableHeight
        = qMax<qreal>(0.0, textEdit->viewport()->height() - (2.0 * document->documentMargin()));

    return qMax(1, int(std::ceil(availableHeight / lineHeight)));
}

static QString tokenAtCursor(QTextCursor cursor)
{
    const int clickedCharPos = cursor.positionInBlock();
    cursor.select(QTextCursor::BlockUnderCursor);
    const QString line = cursor.selectedText().replace("\xc2\xa0", " ");

    static const QRegularExpression tokenRegExp(R"(\b(?<!\.)([^\s]+)\b(?!\.))");
    QRegularExpressionMatchIterator i = tokenRegExp.globalMatch(line);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        if (match.capturedStart() <= clickedCharPos && match.capturedEnd() > clickedCharPos) {
            return match.captured();
        }
    }

    return QString();
}

static QString wordAtCursor(QTextCursor cursor)
{
    cursor.select(QTextCursor::WordUnderCursor);
    return cursor.selectedText();
}

static QString normalizedActionToken(const QString &token)
{
    QString result = token;
    result.replace("\xc2\xa0", " ");
    result = result.trimmed().toLower();
    result.remove(QRegularExpression(QStringLiteral(R"(^[^\w.:]+|[^\w.:]+$)")));
    return result;
}

static bool tokenMatchesXrefTarget(const QString &token, const XrefDescription &xref)
{
    const QString normalizedToken = normalizedActionToken(token);
    if (normalizedToken.isEmpty()) {
        return false;
    }

    const QString address = RAddressString(xref.to).toLower();
    const QString addressNoPrefix = QString::number(xref.to, 16).toLower();
    const QString targetName = normalizedActionToken(xref.to_str);

    return normalizedToken == address || normalizedToken == addressNoPrefix
           || (!targetName.isEmpty()
               && (normalizedToken == targetName || targetName.startsWith(normalizedToken + " ")));
}

static bool xrefIsCall(const XrefDescription &xref)
{
    return xref.type.contains(QStringLiteral("call"), Qt::CaseInsensitive);
}

DisassemblyWidget::DisassemblyWidget(MainWindow *main)
    : MemoryDockWidget(MemoryWidgetType::Disassembly, main)
    , mCtxMenu(new DisassemblyContextMenu(this, main))
    , mDisasTextEdit(new DisassemblyTextEdit(this))
{
    setObjectName(main ? main->getUniqueObjectName(getWidgetType()) : getWidgetType());
    updateWindowTitle();

    topOffset = bottomOffset = RVA_INVALID;
    wheelRemainder = 0;
    cursorLineOffset = 0;
    cursorCharOffset = 0;
    seekFromCursor = false;

    // Instantiate the window layout
    auto *splitter = new QSplitter;
    setMinimumHeight(0);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    splitter->setMinimumHeight(0);
    splitter->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    splitter->setChildrenCollapsible(true);
    // A zero width handle overlaps the listing and blocks accelerated scrolling
    splitter->setHandleWidth(4);

    // Setup the left frame that contains breakpoints and jumps
    leftPanel = new DisassemblyLeftPanel(this);
    leftPanel->setMinimumHeight(0);
    leftPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    splitter->addWidget(leftPanel);

    // Setup the disassembly content
    mDisasTextEdit->setMinimumHeight(0);
    mDisasTextEdit->viewport()->setMinimumHeight(0);
    mDisasTextEdit->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    splitter->addWidget(mDisasTextEdit);
    // Use stylesheet instead of QWidget::setFrameShape(QFrame::NoShape) to
    // avoid issues with dark and light interface themes
    mDisasTextEdit->setStyleSheet("QPlainTextEdit { border: 0px transparent black; }");
    mDisasTextEdit->setFocusProxy(this);
    mDisasTextEdit->setFocusPolicy(Qt::ClickFocus);

    setFocusPolicy(Qt::ClickFocus);

    splitter->setFrameShape(QFrame::NoFrame);
    // Set current widget to the splitted layout we just created
    setWidget(splitter);

    // Resize properly
    QList<int> sizes;
    sizes.append(3);
    sizes.append(1);
    splitter->setSizes(sizes);

    setAllowedAreas(Qt::AllDockWidgetAreas);

    setupFonts();
    setupColors();

    disasmRefresh = createReplacingRefreshDeferrer<RVA>(false, [this](const RVA *offset) {
        refreshDisasm(offset ? *offset : RVA_INVALID);
    });

    maxLines = 0;
    updateMaxLines();

    mDisasTextEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mDisasTextEdit->setFont(Config()->getFont());
    mDisasTextEdit->setReadOnly(true);
    mDisasTextEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    // wrapping breaks readCurrentDisassemblyOffset() at the moment :-(
    mDisasTextEdit->setWordWrapMode(QTextOption::NoWrap);
    mDisasTextEdit->viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    mDisasTextEdit->setCursor(Qt::ArrowCursor);
    mDisasTextEdit->viewport()->setCursor(Qt::ArrowCursor);
    mDisasTextEdit->setMouseTracking(true);
    mDisasTextEdit->viewport()->setMouseTracking(true);
    PreviewTooltip::applyStyleSheet(this);

    // Increase asm text edit margin
    QTextDocument *asm_docu = mDisasTextEdit->document();
    asm_docu->setDocumentMargin(10);

    // Event filter to intercept double clicks in the textbox
    // and showing tooltips when hovering above those offsets
    mDisasTextEdit->viewport()->installEventFilter(this);

    // Set Disas context menu
    mDisasTextEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(
        mDisasTextEdit,
        &QWidget::customContextMenuRequested,
        this,
        &DisassemblyWidget::showDisasContextMenu);

    connectCursorPositionChanged(false);
    connect(mDisasTextEdit->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value != 0) {
            mDisasTextEdit->verticalScrollBar()->setValue(0);
        }
    });

    connect(Core(), &IaitoCore::commentsChanged, this, [this]() { refreshDisasm(); });
    connect(Core(), SIGNAL(flagsChanged()), this, SLOT(refreshDisasm()));
    connect(Core(), &IaitoCore::functionsChanged, this, [this]() {
        clearBasicBlockColorCache();
        refreshDisasm();
    });
    connect(Core(), &IaitoCore::functionRenamed, this, [this]() { refreshDisasm(); });
    connect(Core(), SIGNAL(varsChanged()), this, SLOT(refreshDisasm()));
    connect(Core(), &IaitoCore::asmOptionsChanged, this, [this]() {
        clearBasicBlockColorCache();
        refreshDisasm();
    });
    connect(Core(), &IaitoCore::instructionChanged, this, &DisassemblyWidget::refreshIfInRange);
    connect(Core(), &IaitoCore::breakpointsChanged, this, &DisassemblyWidget::refreshIfInRange);
    connect(Core(), &IaitoCore::refreshCodeViews, this, [this]() {
        clearBasicBlockColorCache();
        refreshDisasm();
    });
    connect(
        Core(),
        &IaitoCore::addressRangeSelectionChanged,
        this,
        &DisassemblyWidget::applyAddressRangeSelection);

    connect(Config(), &Configuration::fontsUpdated, this, &DisassemblyWidget::fontsUpdatedSlot);
    connect(Config(), &Configuration::colorsUpdated, this, &DisassemblyWidget::colorsUpdatedSlot);

    connect(Core(), &IaitoCore::refreshAll, this, [this]() {
        clearBasicBlockColorCache();
        refreshDisasm(seekable->getOffset());
    });
    refreshDisasm(seekable->getOffset());

    connect(mCtxMenu, &DisassemblyContextMenu::copy, mDisasTextEdit, &QPlainTextEdit::copy);
    // Connect copyBytes signal to slot for copying raw instruction bytes
    connect(mCtxMenu, &DisassemblyContextMenu::copyBytes, this, &DisassemblyWidget::copyBytes);

    mCtxMenu->addSeparator();
    mCtxMenu->addAction(&syncAction);
    connect(seekable, &IaitoSeekable::seekableSeekChanged, this, &DisassemblyWidget::on_seekChanged);
    connect(
        mDisasTextEdit,
        &DisassemblyTextEdit::refreshContents,
        this,
        &DisassemblyWidget::on_refreshContents);

    addActions(mCtxMenu->actions());

#define ADD_ACTION(id, ctx, slot) \
    { \
        QAction *a = new QAction(this); \
        ShortcutMgr()->bindAction(id, a); \
        a->setShortcutContext(ctx); \
        addAction(a); \
        connect(a, &QAction::triggered, this, (slot)); \
    }

    ADD_ACTION("disasm.switchToGraph", Qt::WidgetWithChildrenShortcut, [this] {
        mainWindow->showMemoryWidget(MemoryWidgetType::Graph);
    })

    ADD_ACTION("disasm.seekPrev", Qt::WidgetWithChildrenShortcut, &DisassemblyWidget::seekPrev)

    ADD_ACTION("disasm.cursorDown", Qt::WidgetWithChildrenShortcut, [this]() {
        moveCursorRelative(false, false);
    })
    ADD_ACTION("disasm.cursorUp", Qt::WidgetWithChildrenShortcut, [this]() {
        moveCursorRelative(true, false);
    })
    ADD_ACTION("disasm.cursorPageDown", Qt::WidgetWithChildrenShortcut, [this]() {
        moveCursorRelative(false, true);
    })
    ADD_ACTION("disasm.cursorPageUp", Qt::WidgetWithChildrenShortcut, [this]() {
        moveCursorRelative(true, true);
    })
#undef ADD_ACTION
}

void DisassemblyWidget::setPreviewMode(bool previewMode)
{
    mDisasTextEdit->setContextMenuPolicy(previewMode ? Qt::NoContextMenu : Qt::CustomContextMenu);
    mCtxMenu->setEnabled(!previewMode);
    for (auto action : mCtxMenu->actions()) {
        action->setEnabled(!previewMode);
    }
    for (auto action : actions()) {
        if (action->shortcut() == Qt::Key_Space || action->shortcut() == Qt::Key_Escape) {
            action->setEnabled(!previewMode);
        }
    }
    if (previewMode) {
        seekable->setSynchronization(false);
    }
}

QSize DisassemblyWidget::minimumSizeHint() const
{
    QSize hint = MemoryDockWidget::minimumSizeHint();
    hint.setHeight(0);
    return hint;
}

QWidget *DisassemblyWidget::getTextWidget()
{
    return mDisasTextEdit;
}

QString DisassemblyWidget::getWidgetType()
{
    return "Disassembly";
}

QFontMetrics DisassemblyWidget::getFontMetrics()
{
    return mDisasTextEdit->fontMetrics();
}

QList<DisassemblyLine> DisassemblyWidget::getLines()
{
    return lines;
}

const QMap<RVA, RVA> &DisassemblyWidget::getFunctionRanges() const
{
    return functionRanges;
}

static void applySpanStyle(QTextCharFormat &fmt, const QString &style)
{
    const auto entries = style.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        const int colon = entry.indexOf(QLatin1Char(':'));
        if (colon < 0) {
            continue;
        }
        const QString key = entry.left(colon).trimmed();
        const QString value = entry.mid(colon + 1);
        if (key == QLatin1String("color")) {
            fmt.setForeground(QColor(value.trimmed()));
        } else if (key == QLatin1String("background-color")) {
            fmt.setBackground(QColor(value.trimmed()));
        } else if (key == QLatin1String("font-weight")) {
            fmt.setFontWeight(QFont::Bold);
        } else if (key == QLatin1String("text-decoration")) {
            fmt.setFontUnderline(true);
            fmt.setFontOverline(true);
        }
    }
}

// Inserts r_str_html_strip() output without Qt's HTML parser, false on other markup
static bool insertStrippedHtml(QTextCursor &cursor, const QString &html)
{
    const QTextCharFormat base;
    QTextCharFormat fmt = base;
    QString run;
    run.reserve(html.size());
    const auto flush = [&]() {
        if (!run.isEmpty()) {
            cursor.insertText(run, fmt);
            run.clear();
        }
    };
    const int n = html.size();
    int i = 0;
    while (i < n) {
        const QChar c = html.at(i);
        if (c == QLatin1Char('<')) {
            const int end = html.indexOf(QLatin1Char('>'), i);
            if (end < 0) {
                return false;
            }
            const QStringView tag = QStringView(html).mid(i + 1, end - i - 1);
            if (tag.startsWith(QLatin1String("span"))) {
                flush();
                fmt = base;
                const int q1 = tag.indexOf(QLatin1Char('\''));
                const int q2 = q1 < 0 ? -1 : tag.indexOf(QLatin1Char('\''), q1 + 1);
                if (q1 >= 0 && q2 > q1) {
                    applySpanStyle(fmt, tag.mid(q1 + 1, q2 - q1 - 1).toString());
                }
            } else if (tag == QLatin1String("/span")) {
                flush();
                fmt = base;
            } else if (tag.startsWith(QLatin1String("br"))) {
                run.append(QChar::LineSeparator);
            } else {
                return false;
            }
            i = end + 1;
            continue;
        }
        if (c == QLatin1Char('&')) {
            const QStringView rest = QStringView(html).mid(i);
            if (rest.startsWith(QLatin1String("&nbsp;"))) {
                run.append(QLatin1Char(' '));
                i += 6;
            } else if (rest.startsWith(QLatin1String("&lt;"))) {
                run.append(QLatin1Char('<'));
                i += 4;
            } else if (rest.startsWith(QLatin1String("&gt;"))) {
                run.append(QLatin1Char('>'));
                i += 4;
            } else if (rest.startsWith(QLatin1String("&amp;"))) {
                run.append(QLatin1Char('&'));
                i += 5;
            } else {
                run.append(c);
                i++;
            }
            continue;
        }
        if (c == QLatin1Char('\n')) {
            run.append(QChar::LineSeparator);
        } else if (c != QLatin1Char('\r')) {
            run.append(c);
        }
        i++;
    }
    flush();
    return true;
}

static int indexOfLine(const QList<DisassemblyLine> &listing, RVA offset)
{
    for (int i = 0; i < listing.size(); i++) {
        if (listing.at(i).offset == offset) {
            return i;
        }
        if (listing.at(i).offset > offset) {
            break;
        }
    }
    return -1;
}

void DisassemblyWidget::refreshIfInRange(RVA offset)
{
    if (offset >= topOffset && offset <= bottomOffset) {
        refreshDisasm();
    }
}

void DisassemblyWidget::refreshDisasm(RVA offset)
{
    // Only the scroll that set it may reuse the cache, never a deferred refresh
    const bool cached = useCachedLines;
    useCachedLines = false;
    if (!disasmRefresh->attemptRefresh(offset == RVA_INVALID ? nullptr : new RVA(offset))) {
        return;
    }

    if (offset != RVA_INVALID) {
        topOffset = offset;
    }

    if (topOffset == RVA_INVALID) {
        return;
    }

    topOffset = Core()->alignInstructionAddress(topOffset);

    if (maxLines <= 0) {
        connectCursorPositionChanged(true);
        mDisasTextEdit->clear();
        connectCursorPositionChanged(false);
        visibleLines = 0;
        return;
    }

    breakpoints = Core()->getBreakpointsAddresses();

    // Retrieve disassembly lines
    outgoingXRefsCache.clear();
    const QList<DisassemblyLine> oldLines = lines;
    if (cached) {
        int index = indexOfLine(cachedLines, topOffset);
        if (index < 0 || cachedLines.size() - index < maxLines) {
            // Miss while scrolling: fetch ahead so the next scrolls are free.
            cachedLines = fetchLines(topOffset, maxLines * 3);
            index = 0;
        }
        lines = cachedLines.mid(index, maxLines);
    } else {
        cachedLines = fetchLines(topOffset, maxLines);
        lines = cachedLines.mid(0, maxLines);
    }

    functionRanges.clear();
    RVA previousOffset = RVA_INVALID;
    for (const auto &line : lines) {
        if (line.offset == previousOffset) {
            continue;
        }
        previousOffset = line.offset;

        auto range = functionRanges.upperBound(line.offset);
        if (range != functionRanges.begin() && line.offset < (--range).value()) {
            continue;
        }

        RVA start = Core()->getFunctionStart(line.offset);
        if (start == RVA_INVALID) {
            continue;
        }
        RVA end = Core()->getFunctionEnd(start);
        if (end != RVA_INVALID && start < end) {
            functionRanges.insert(start, end);
        }
    }

    int newVisible = qMin(lines.size(), maxLines);
    for (int i = 0; i < newVisible; ++i) {
        if (lines.at(i).offset < topOffset) { // overflow
            newVisible = i;
            break;
        }
    }

    if (lines.isEmpty()) {
        bottomOffset = topOffset;
    } else {
        bottomOffset = lines[qMin(lines.size(), maxLines) - 1].offset;
        if (bottomOffset < topOffset) {
            bottomOffset = RVA_MAX;
        }
    }

    if (!cached || !updateDocumentIncrementally(oldLines, newVisible)) {
        rebuildDocument(newVisible);
    }
}

void DisassemblyWidget::decorateBlock(
    const QTextBlock &block, const DisassemblyLine &line, BasicBlockColor &bbColor)
{
    QTextCursor cursor(block);
    QTextBlockFormat f;
    if (Core()->isBreakpoint(breakpoints, line.offset)) {
        f.setBackground(ConfigColor("gui.breakpoint_background"));
    } else {
        if (line.offset < bbColor.start || line.offset >= bbColor.end) {
            bbColor = getBasicBlockColor(line.offset);
        }
        if (bbColor.color.isValid()) {
            QLinearGradient grad(1.0, 0.0, 0.2, 0.0);
            grad.setCoordinateMode(QGradient::ObjectBoundingMode);
            QColor tintStart = bbColor.color;
            tintStart.setAlpha(110);
            QColor tintEnd = bbColor.color;
            tintEnd.setAlpha(0);
            grad.setColorAt(0.0, tintStart);
            grad.setColorAt(1.0, tintEnd);
            f.setBackground(QBrush(grad));
        }
    }
    cursor.setBlockFormat(f);
    QTextBlock b = block;
    b.setUserData(new DisassemblyTextBlockUserData(line));
    b.layout()->setCacheEnabled(true);
}

void DisassemblyWidget::insertLineText(QTextCursor &cursor, const DisassemblyLine &line)
{
    if (!insertStrippedHtml(cursor, line.text)) {
        cursor.insertHtml(line.text);
    }
}

static bool sameLines(
    const QList<DisassemblyLine> &a, int ai, const QList<DisassemblyLine> &b, int bi, int count)
{
    if (ai + count > a.size() || bi + count > b.size()) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const DisassemblyLine &la = a.at(ai + i);
        const DisassemblyLine &lb = b.at(bi + i);
        if (la.offset != lb.offset || la.arrow != lb.arrow || la.text != lb.text) {
            return false;
        }
    }
    return true;
}

// Checks that every block still carries the user data of its line
bool DisassemblyWidget::documentMatchesLines(const QList<DisassemblyLine> &ref, int count)
{
    QTextDocument *doc = mDisasTextEdit->document();
    if (doc->blockCount() != count || ref.size() < count) {
        return false;
    }
    QTextBlock block = doc->firstBlock();
    for (int i = 0; i < count; i++, block = block.next()) {
        auto *data = static_cast<DisassemblyTextBlockUserData *>(block.userData());
        if (!data || data->line.offset != ref.at(i).offset) {
            return false;
        }
    }
    return true;
}

// Scrolls by editing only the rows that changed and blitting the rest of the view
bool DisassemblyWidget::updateDocumentIncrementally(
    const QList<DisassemblyLine> &oldLines, int newVisible)
{
    QTextDocument *doc = mDisasTextEdit->document();
    const int oldVisible = visibleLines;
    if (oldVisible <= 0 || newVisible <= 0 || oldLines.isEmpty()
        || doc->blockCount() != oldVisible) {
        return false;
    }
    if (!documentMatchesLines(oldLines, oldVisible)) {
        return false;
    }

    int shift = 0;
    int keep = 0;
    if (lines.first().offset > oldLines.first().offset) {
        shift = indexOfLine(oldLines, lines.first().offset);
        if (shift <= 0 || shift >= oldVisible) {
            return false;
        }
        keep = oldVisible - shift;
        if (keep > newVisible || !sameLines(oldLines, shift, lines, 0, keep)) {
            return false;
        }
    } else if (lines.first().offset < oldLines.first().offset) {
        shift = -indexOfLine(lines, oldLines.first().offset);
        if (shift >= 0 || -shift >= newVisible) {
            return false;
        }
        keep = qMin(oldVisible, newVisible + shift);
        if (keep <= 0 || !sameLines(oldLines, 0, lines, -shift, keep)) {
            return false;
        }
    } else {
        return false;
    }

    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    const bool layoutSignalsBlocked = layout->blockSignals(true);
    connectCursorPositionChanged(true);
    mDisasTextEdit->setLockScroll(true);

    QTextCursor cursor(doc);
    const QTextBlockFormat regular = cursor.blockFormat();
    BasicBlockColor bbColor;
    qreal delta = 0;
    cursor.beginEditBlock();
    if (shift > 0) {
        delta = mDisasTextEdit->blockTop(doc->findBlockByNumber(shift))
                - mDisasTextEdit->blockTop(doc->firstBlock());
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, shift);
        cursor.removeSelectedText();
        decorateBlock(doc->firstBlock(), lines.at(0), bbColor);
        cursor.movePosition(QTextCursor::End);
        for (int i = keep; i < newVisible; i++) {
            cursor.insertBlock();
            cursor.setBlockFormat(regular);
            insertLineText(cursor, lines.at(i));
            decorateBlock(cursor.block(), lines.at(i), bbColor);
        }
    } else {
        const int prepend = -shift;
        if (keep < oldVisible) {
            const QTextBlock last = doc->findBlockByNumber(keep - 1);
            cursor.setPosition(last.position() + last.length() - 1);
            cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        }
        cursor.setPosition(0);
        for (int i = 0; i < prepend; i++) {
            insertLineText(cursor, lines.at(i));
            cursor.insertBlock();
            cursor.setBlockFormat(regular);
        }
        // Splitting blocks hands the old decorations to the new rows, redo them
        QTextBlock block = doc->firstBlock();
        for (int i = 0; i <= prepend && block.isValid(); i++, block = block.next()) {
            decorateBlock(block, lines.at(i), bbColor);
        }
        if (keep < oldVisible) {
            decorateBlock(doc->findBlockByNumber(newVisible - 1), lines.at(newVisible - 1), bbColor);
        }
    }
    cursor.endEditBlock();
    // Reused block nodes keep the layout of their previous text, recompute it
    for (int i = 0; i <= qMax(0, -shift); i++) {
        doc->findBlockByNumber(i).clearLayout();
    }
    doc->lastBlock().clearLayout();
    if (shift < 0) {
        delta = -(
            mDisasTextEdit->blockTop(doc->findBlockByNumber(-shift))
            - mDisasTextEdit->blockTop(doc->firstBlock()));
    }
    layout->blockSignals(layoutSignalsBlocked);
    mDisasTextEdit->setLockScroll(false);
    connectCursorPositionChanged(false);

    visibleLines = newVisible;
    if (!documentMatchesLines(lines, newVisible)) {
        return false; // the full rebuild repairs whatever went wrong
    }

    // Blit everything below the document margin so only the new rows get painted
    QWidget *viewport = mDisasTextEdit->viewport();
    const int margin = qMax(0, qCeil(mDisasTextEdit->textOffset()));
    if (qAbs(delta - qRound(delta)) > 0.01) {
        viewport->update();
    } else {
        viewport->scroll(
            0, -qRound(delta), QRect(0, margin, viewport->width(), viewport->height() - margin));
    }
    updateCursorPosition();
    if (readCurrentDisassemblyOffset() == seekable->getOffset()) {
        // The cursor did not move, extend the highlights to the new rows
        highlightCurrentLine();
        highlightPCLine();
    }
    if (Core()->hasAddressRangeSelection()) {
        applyAddressRangeSelection(
            Core()->getAddressRangeSelectionStart(), Core()->getAddressRangeSelectionEnd());
    }
    leftPanel->update();
    return true;
}

void DisassemblyWidget::rebuildDocument(int newVisible)
{
    int horizontalScrollValue = mDisasTextEdit->horizontalScrollBar()->value();
    mDisasTextEdit->setLockScroll(true); // avoid flicker
    mDisasTextEdit->setUpdatesEnabled(false);
    connectCursorPositionChanged(true);

    mDisasTextEdit->document()->clear();
    QTextCursor cursor(mDisasTextEdit->document());
    QTextBlockFormat regular = cursor.blockFormat();
    cursor.beginEditBlock();

    BasicBlockColor bbColor;
    for (int i = 0; i < newVisible; ++i) {
        const DisassemblyLine &line = lines.at(i);
        insertLineText(cursor, line);
        decorateBlock(cursor.block(), line, bbColor);
        if (i + 1 < newVisible) {
            cursor.insertBlock();
            cursor.setBlockFormat(regular);
        }
    }
    cursor.endEditBlock();
    visibleLines = newVisible;

    connectCursorPositionChanged(false);
    updateCursorPosition();

    mDisasTextEdit->setLockScroll(false);
    mDisasTextEdit->horizontalScrollBar()->setValue(horizontalScrollValue);
    mDisasTextEdit->setUpdatesEnabled(true);

    // Refresh the left panel (trigger paintEvent)
    if (Core()->hasAddressRangeSelection()) {
        applyAddressRangeSelection(
            Core()->getAddressRangeSelectionStart(), Core()->getAddressRangeSelectionEnd());
    }
    leftPanel->update();
}

/**
 * @brief Scroll the listing by `count` rendered lines (negative scrolls back).
 *
 * The view is drawn with `pdJ`, which yields one entry per *line*, while a
 * single address may render several of them (comments, xrefs, labels). Moving
 * the top address by instructions would therefore scroll a different amount of
 * lines up than down, so pick the address of the line `count` rows away and let
 * both directions agree.
 */
void DisassemblyWidget::scrollLines(int count)
{
    if (count == 0 || topOffset == RVA_INVALID) {
        return;
    }

    QList<DisassemblyLine> listing = cachedLines;
    int index = indexOfLine(listing, topOffset);
    if (index < 0) {
        listing = lines;
        index = 0;
    }
    if (count > 0) {
        index += count;
    } else {
        // The short exact step back is the anchor the cached lines must contain
        const int back = -count;
        const RVA start = Core()->prevOpAddr(topOffset, back);
        if (start >= topOffset) {
            return;
        }
        int startIndex = indexOfLine(listing, start);
        if (startIndex < 0 || startIndex >= index) {
            // Not cached: fetch from a page above so the next scrolls up hit the cache
            const RVA farStart = Core()->prevOpAddr(start, maxLines);
            if (farStart < start) {
                listing = fetchLines(farStart, maxLines * 3 + back);
                startIndex = indexOfLine(listing, start);
                index = indexOfLine(listing, topOffset);
            }
            if (startIndex < 0 || index < 0 || startIndex >= index) {
                listing = fetchLines(start, back * 2 + maxLines);
                startIndex = 0;
                index = 0;
                for (int i = 0; i < listing.size(); i++) {
                    if (listing.at(i).offset >= topOffset) {
                        index = i;
                        break;
                    }
                }
            }
        }
        index = qMax(startIndex, index - back);
    }

    RVA offset = RVA_INVALID;
    if (!listing.isEmpty()) {
        index = qBound(0, index, listing.size() - 1);
        // The listing always starts at the first line of an address, so snap to
        // it instead of scrolling into the middle of a group of lines.
        while (index > 0 && listing.at(index - 1).offset == listing.at(index).offset) {
            index--;
        }
        offset = listing.at(index).offset;
    }
    if (offset == RVA_INVALID || offset == topOffset) {
        // Nothing usable to jump to, step a single instruction so that a long
        // run of lines sharing one address can never block the scroll.
        offset = (count > 0) ? Core()->nextOpAddr(topOffset, 1) : Core()->prevOpAddr(topOffset, 1);
    }

    // Require strict progress in the requested direction; otherwise stay put
    // instead of teleporting to 0/RVA_MAX as the old guard did.
    if ((count > 0 && offset <= topOffset) || (count < 0 && offset >= topOffset)) {
        return;
    }
    cachedLines = listing;
    useCachedLines = true;
    refreshDisasm(offset);
}

QList<DisassemblyLine> DisassemblyWidget::fetchLines(RVA offset, int count)
{
    TempConfig tempConfig;
    tempConfig.set("scr.color", COLOR_MODE_16M).set("asm.lines", false).set("asm.trace.color", false);
    return Core()->disassembleLines(offset, count);
}

/**
 * @brief Turn a wheel event into a line scroll, shared by the listing and the
 * arrows panel so that both scroll by the same amount.
 */
void DisassemblyWidget::wheelScroll(QWheelEvent *event)
{
    const qreal notches = event->angleDelta().y() / 120.0;
    if (notches * wheelRemainder < 0) { // direction changed, drop the leftover
        wheelRemainder = 0;
    }
    wheelRemainder += notches * QApplication::wheelScrollLines();
    const int count = int(wheelRemainder);
    wheelRemainder -= count;
    // Never scroll more than a screenful at once, no matter how the system
    // wheel settings are configured.
    const int limit = qMax(1, maxLines);
    scrollLines(-qBound(-limit, count, limit));
}

bool DisassemblyWidget::updateMaxLines()
{
    int currentMaxLines = getMaxVisibleDisassemblyLines(mDisasTextEdit);

    if (currentMaxLines != maxLines) {
        maxLines = currentMaxLines;
        refreshDisasm();
        return true;
    }

    return false;
}

DisassemblyWidget::BasicBlockColor DisassemblyWidget::getBasicBlockColor(RVA offset)
{
    for (int i = basicBlockColorCache.size() - 1; i >= 0; --i) {
        const BasicBlockColor &cached = basicBlockColorCache.at(i);
        if (offset >= cached.start && offset < cached.end) {
            return cached;
        }
    }

    BasicBlockColor result;
    result.start = offset;
    result.end = offset + 1;

    if (!Core()->functionIn(offset)) {
        return result;
    }

    const QString bbJson = Core()->cmd(QStringLiteral("abj @ %1").arg(offset));
    const QJsonDocument doc = QJsonDocument::fromJson(bbJson.toUtf8());
    const QJsonArray arr = doc.array();
    if (!arr.isEmpty()) {
        const QJsonObject bb = arr.first().toObject();
        result.start = bb.value(QStringLiteral("addr")).toVariant().toULongLong();
        const RVA size = bb.value(QStringLiteral("size")).toVariant().toULongLong();
        result.end = result.start + qMax<RVA>(size, 1);

        if (auto *block = Core()->getBBHighlighter()->getBasicBlock(result.start)) {
            result.color = block->color;
        } else {
            const QString colorStr = Core()->cmd(QStringLiteral("abc @ %1").arg(result.start));
            if (colorStr.length() > 6) {
                const QColor c(QStringLiteral("#") + colorStr.mid(1, 6));
                if (c.isValid()) {
                    result.color = c;
                }
            }
        }
    }

    basicBlockColorCache.append(result);
    constexpr int maxCachedBlocks = 2048;
    if (basicBlockColorCache.size() > maxCachedBlocks) {
        basicBlockColorCache.erase(
            basicBlockColorCache.begin(),
            basicBlockColorCache.begin() + (basicBlockColorCache.size() - maxCachedBlocks));
    }
    return result;
}

void DisassemblyWidget::clearBasicBlockColorCache()
{
    basicBlockColorCache.clear();
}

void DisassemblyWidget::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extraSelections;
    QColor highlightColor = ConfigColor("lineHighlight");

    // Highlight the current word
    QTextCursor cursor = mDisasTextEdit->textCursor();
    auto clickedCharPos = cursor.positionInBlock();
    // Select the line (BlockUnderCursor matches a line with current
    // implementation)
    cursor.select(QTextCursor::BlockUnderCursor);
    // Remove any non-breakable space from the current line
    QString searchString = cursor.selectedText().replace("\xc2\xa0", " ");
    // Cut the line in "tokens" that can be highlighted
    static const QRegularExpression tokenRegExp(R"(\b(?<!\.)([^\s]+)\b(?!\.))");
    QRegularExpressionMatchIterator i = tokenRegExp.globalMatch(searchString);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        // Current token is under our cursor, select this one
        if (match.capturedStart() <= clickedCharPos && match.capturedEnd() > clickedCharPos) {
            curHighlightedWord = match.captured();
            break;
        }
    }

    // Highlight the current line
    QTextEdit::ExtraSelection highlightSelection;
    highlightSelection.cursor = cursor;
    highlightSelection.cursor.movePosition(QTextCursor::Start);
    while (true) {
        RVA lineOffset = readDisassemblyOffset(highlightSelection.cursor);
        if (lineOffset == seekable->getOffset()) {
            highlightSelection.format.setBackground(highlightColor);
            highlightSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
            highlightSelection.cursor.clearSelection();
            extraSelections.append(highlightSelection);
        } else if (lineOffset != RVA_INVALID && lineOffset > seekable->getOffset()) {
            break;
        }
        highlightSelection.cursor.movePosition(QTextCursor::EndOfLine);
        if (highlightSelection.cursor.atEnd()) {
            break;
        }

        highlightSelection.cursor.movePosition(QTextCursor::Down);
    }

    // Highlight all the words in the document same as the current one
    extraSelections.append(createSameWordsSelections(mDisasTextEdit, curHighlightedWord));

    // Highlight matches from scr.highlight
    QString scrHighlight = Core()->getConfig("scr.highlight");
    extraSelections.append(createHighlightSelections(mDisasTextEdit, scrHighlight));

    mDisasTextEdit->setExtraSelections(extraSelections);
}

void DisassemblyWidget::highlightPCLine()
{
    RVA PCAddr = Core()->getProgramCounterValue();

    QColor highlightPCColor = ConfigColor("highlightPC");

    QList<QTextEdit::ExtraSelection> pcSelections;
    QTextEdit::ExtraSelection highlightSelection;
    highlightSelection.cursor = mDisasTextEdit->textCursor();
    highlightSelection.cursor.movePosition(QTextCursor::Start);
    if (PCAddr != RVA_INVALID) {
        while (true) {
            RVA lineOffset = readDisassemblyOffset(highlightSelection.cursor);
            if (lineOffset == PCAddr) {
                highlightSelection.format.setBackground(highlightPCColor);
                highlightSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
                highlightSelection.cursor.clearSelection();
                pcSelections.append(highlightSelection);
            } else if (lineOffset != RVA_INVALID && lineOffset > PCAddr) {
                break;
            }
            highlightSelection.cursor.movePosition(QTextCursor::EndOfLine);
            if (highlightSelection.cursor.atEnd()) {
                break;
            }

            highlightSelection.cursor.movePosition(QTextCursor::Down);
        }
    }

    // Don't override any extraSelections already set
    QList<QTextEdit::ExtraSelection> currentSelections = mDisasTextEdit->extraSelections();
    currentSelections.append(pcSelections);

    mDisasTextEdit->setExtraSelections(currentSelections);
}

void DisassemblyWidget::showDisasContextMenu(const QPoint &pt)
{
    RVA currentOffset = readCurrentDisassemblyOffset();
    if (currentOffset != RVA_INVALID) {
        mCtxMenu->setOffset(currentOffset);
    }
    updateContextMenuState();

    RVA selectionStart = RVA_INVALID;
    int selectionSize = 0;
    if (getSelectedInstructionRange(&selectionStart, &selectionSize)) {
        mCtxMenu->setSelectionRange(selectionStart, selectionSize);
    }

    mCtxMenu->exec(mDisasTextEdit->mapToGlobal(pt));
}

RVA DisassemblyWidget::readCurrentDisassemblyOffset()
{
    QTextCursor tc = mDisasTextEdit->textCursor();
    return readDisassemblyOffset(tc);
}

RVA DisassemblyWidget::readDisassemblyOffset(QTextCursor tc)
{
    auto userData = getUserData(tc.block());
    if (!userData) {
        return RVA_INVALID;
    }

    return userData->line.offset;
}

bool DisassemblyWidget::getSelectedInstructionRange(RVA *startOffset, int *size)
{
    if (!startOffset || !size) {
        return false;
    }

    *startOffset = RVA_INVALID;
    *size = 0;

    QTextCursor cursor = mDisasTextEdit->textCursor();
    if (!cursor.hasSelection()) {
        return false;
    }

    int selStart = cursor.selectionStart();
    int selEnd = cursor.selectionEnd();
    if (selEnd <= selStart) {
        return false;
    }

    selEnd -= 1;

    QTextCursor startCursor = cursor;
    startCursor.setPosition(selStart);
    QTextCursor endCursor = cursor;
    endCursor.setPosition(selEnd);

    RVA rangeStart = readDisassemblyOffset(startCursor);
    RVA rangeEnd = readDisassemblyOffset(endCursor);
    if (rangeStart == RVA_INVALID || rangeEnd == RVA_INVALID) {
        return false;
    }
    if (rangeStart > rangeEnd) {
        std::swap(rangeStart, rangeEnd);
    }

    QJsonArray instArray = Core()->cmdj(QStringLiteral("aoj @ %1").arg(rangeEnd)).array();
    if (instArray.isEmpty()) {
        return false;
    }

    int instrSize = instArray.first().toObject().value("size").toInt(1);
    if (instrSize <= 0) {
        instrSize = 1;
    }

    RVA byteCount = rangeEnd - rangeStart + static_cast<RVA>(instrSize);
    if (byteCount > static_cast<RVA>(std::numeric_limits<int>::max())) {
        return false;
    }

    *startOffset = rangeStart;
    *size = static_cast<int>(byteCount);
    return *size > 0;
}

void DisassemblyWidget::publishAddressRangeSelection()
{
    if (applyingAddressRangeSelection || !Config()->getAddressRangeSelectionSyncEnabled()) {
        return;
    }

    RVA selectionStart = RVA_INVALID;
    int selectionSize = 0;
    if (!getSelectedInstructionRange(&selectionStart, &selectionSize)) {
        publishingAddressRangeSelection = true;
        Core()->clearAddressRangeSelection();
        publishingAddressRangeSelection = false;
        return;
    }

    const RVA byteCount = static_cast<RVA>(selectionSize);
    if (byteCount == 0 || selectionStart > std::numeric_limits<RVA>::max() - (byteCount - 1)) {
        publishingAddressRangeSelection = true;
        Core()->clearAddressRangeSelection();
        publishingAddressRangeSelection = false;
        return;
    }

    publishingAddressRangeSelection = true;
    Core()->setAddressRangeSelection(selectionStart, selectionStart + byteCount - 1);
    publishingAddressRangeSelection = false;
}

void DisassemblyWidget::applyAddressRangeSelection(RVA start, RVA end)
{
    if (publishingAddressRangeSelection) {
        return;
    }
    const bool hasRange = start != RVA_INVALID && end != RVA_INVALID && start <= end;

    applyingAddressRangeSelection = true;
    connectCursorPositionChanged(true);

    QTextCursor cursor = mDisasTextEdit->textCursor();
    cursor.clearSelection();

    if (hasRange) {
        const RVA alignedStart = Core()->alignInstructionAddress(start);
        QTextBlock firstBlock;
        QTextBlock lastBlock;

        for (QTextBlock block = mDisasTextEdit->document()->begin(); block.isValid();
             block = block.next()) {
            QTextCursor blockCursor(block);
            const RVA lineOffset = readDisassemblyOffset(blockCursor);
            if (lineOffset == RVA_INVALID || lineOffset < alignedStart || lineOffset > end) {
                continue;
            }
            if (!firstBlock.isValid()) {
                firstBlock = block;
            }
            lastBlock = block;
        }

        if (firstBlock.isValid() && lastBlock.isValid()) {
            cursor = QTextCursor(mDisasTextEdit->document());
            cursor.setPosition(firstBlock.position());

            QTextCursor endCursor(lastBlock);
            endCursor.movePosition(QTextCursor::EndOfBlock);
            cursor.setPosition(endCursor.position(), QTextCursor::KeepAnchor);
        }
    }

    mDisasTextEdit->setTextCursor(cursor);
    connectCursorPositionChanged(false);
    applyingAddressRangeSelection = false;
}

void DisassemblyWidget::updateCursorPosition()
{
    RVA offset = seekable->getOffset();

    // already fine where it is?
    RVA currentLineOffset = readCurrentDisassemblyOffset();
    if (currentLineOffset == offset) {
        return;
    }

    connectCursorPositionChanged(true);

    if (offset < topOffset || (offset > bottomOffset && bottomOffset != RVA_INVALID)) {
        const QTextCursor parked = mDisasTextEdit->textCursor();
        if (parked.position() != 0 || parked.hasSelection()) {
            mDisasTextEdit->moveCursor(QTextCursor::Start);
        }
        mDisasTextEdit->setExtraSelections(
            createSameWordsSelections(mDisasTextEdit, curHighlightedWord));
    } else {
        RVA currentCursorOffset = readCurrentDisassemblyOffset();
        QTextCursor originalCursor = mDisasTextEdit->textCursor();

        QTextCursor cursor = originalCursor;
        cursor.movePosition(QTextCursor::Start);

        while (true) {
            RVA lineOffset = readDisassemblyOffset(cursor);
            if (lineOffset == offset) {
                if (cursorLineOffset > 0) {
                    cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, cursorLineOffset);
                }
                if (cursorCharOffset > 0) {
                    cursor.movePosition(QTextCursor::StartOfLine);
                    cursor
                        .movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, cursorCharOffset);
                }

                mDisasTextEdit->setTextCursor(cursor);
                highlightCurrentLine();
                break;
            } else if (lineOffset != RVA_INVALID && lineOffset > offset) {
                mDisasTextEdit->moveCursor(QTextCursor::Start);
                mDisasTextEdit->setExtraSelections({});
                break;
            }

            cursor.movePosition(QTextCursor::EndOfLine);
            if (cursor.atEnd()) {
                break;
            }

            cursor.movePosition(QTextCursor::Down);
        }

        // this is true if a seek came from the user clicking on a line.
        // then the cursor should be restored 1:1 to retain selection and cursor
        // position.
        if (currentCursorOffset == offset) {
            mDisasTextEdit->setTextCursor(originalCursor);
        }
    }

    highlightPCLine();

    connectCursorPositionChanged(false);
}

void DisassemblyWidget::connectCursorPositionChanged(bool disconnect)
{
    if (disconnect) {
        QObject::disconnect(
            mDisasTextEdit,
            &QPlainTextEdit::cursorPositionChanged,
            this,
            &DisassemblyWidget::cursorPositionChanged);
    } else {
        connect(
            mDisasTextEdit,
            &QPlainTextEdit::cursorPositionChanged,
            this,
            &DisassemblyWidget::cursorPositionChanged);
    }
}

void DisassemblyWidget::cursorPositionChanged()
{
    RVA offset = readCurrentDisassemblyOffset();

    cursorLineOffset = 0;
    QTextCursor c = mDisasTextEdit->textCursor();
    cursorCharOffset = c.positionInBlock();
    while (c.blockNumber() > 0) {
        c.movePosition(QTextCursor::PreviousBlock);
        if (readDisassemblyOffset(c) != offset) {
            break;
        }
        cursorLineOffset++;
    }

    seekFromCursor = true;
    seekable->seek(offset);
    seekFromCursor = false;
    highlightCurrentLine();
    highlightPCLine();
    publishAddressRangeSelection();
    updateContextMenuState();
    leftPanel->update();
}

void DisassemblyWidget::updateContextMenuState()
{
    QTextCursor cursor = mDisasTextEdit->textCursor();
    bool hasSelection = cursor.hasSelection();

    mCtxMenu->setCanCopy(hasSelection);
    mCtxMenu->setSelectionRange(RVA_INVALID, 0);

    if (hasSelection) {
        // A word is selected so use it
        mCtxMenu->setCurHighlightedWord(cursor.selectedText());
    } else {
        // No word is selected so use the word under the cursor
        mCtxMenu->setCurHighlightedWord(curHighlightedWord);
    }
}

void DisassemblyWidget::moveCursorRelative(bool up, bool page)
{
    if (page) {
        RVA offset;
        if (!up) {
            offset = Core()->nextOpAddr(bottomOffset, 1);
            if (offset <= bottomOffset) {
                return;
            }
            useCachedLines = true;
        } else {
            offset = Core()->prevOpAddr(topOffset, maxLines);
            if (offset >= topOffset) {
                return;
            } else {
                // disassembly from calculated offset may have more than
                // maxLines lines move some instructions down if necessary.

                auto lines = Core()->disassembleLines(offset, maxLines).toVector();
                int oldTopLine;
                for (oldTopLine = lines.length(); oldTopLine > 0; oldTopLine--) {
                    if (lines[oldTopLine - 1].offset < topOffset) {
                        break;
                    }
                }

                int overflowLines = oldTopLine - maxLines;
                if (overflowLines > 0) {
                    while (lines[overflowLines - 1].offset == lines[overflowLines].offset
                           && overflowLines < lines.length() - 1) {
                        overflowLines++;
                    }
                    offset = lines[overflowLines].offset;
                }
            }
        }
        refreshDisasm(offset);
    } else { // normal arrow keys
        int blockCount = mDisasTextEdit->blockCount();
        if (blockCount < 1) {
            return;
        }

        int blockNumber = mDisasTextEdit->textCursor().blockNumber();

        if (blockNumber == blockCount - 1 && !up) {
            scrollLines(1);
        } else if (blockNumber == 0 && up) {
            scrollLines(-1);
        }

        mDisasTextEdit->moveCursor(up ? QTextCursor::Up : QTextCursor::Down);

        // handle cases where top instruction offsets change
        RVA offset = readCurrentDisassemblyOffset();
        if (offset != seekable->getOffset()) {
            seekable->seek(offset);
            highlightCurrentLine();
            highlightPCLine();
        }
    }
}
// Slot to handle copying raw bytes of selected instructions
void DisassemblyWidget::copyBytes()
{
    RVA startOffset = RVA_INVALID;
    int count = 0;
    if (!getSelectedInstructionRange(&startOffset, &count)) {
        return;
    }

    // Fetch bytes in hex format
    QString bytesStr = Core()->cmdRawAt(QStringLiteral("p8 %1").arg(count), startOffset).trimmed();
    // Copy to clipboard
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(bytesStr);
}

void DisassemblyWidget::jumpToOffsetUnderCursor(const QTextCursor &cursor)
{
    RVA offset = readDisassemblyOffset(cursor);
    seekable->seekToReference(offset);
}

QList<XrefDescription> DisassemblyWidget::getOutgoingXRefs(RVA offset)
{
    if (offset == RVA_INVALID) {
        return {};
    }

    auto it = outgoingXRefsCache.constFind(offset);
    if (it != outgoingXRefsCache.constEnd()) {
        return *it;
    }

    QList<XrefDescription> refs = Core()->getXRefs(offset, false, false);
    outgoingXRefsCache.insert(offset, refs);
    return refs;
}

RVA DisassemblyWidget::xrefTargetForToken(RVA offset, const QString &token)
{
    if (normalizedActionToken(token).isEmpty()) {
        return RVA_INVALID;
    }

    const QList<XrefDescription> refs = getOutgoingXRefs(offset);
    for (const XrefDescription &ref : refs) {
        if (ref.to != RVA_INVALID && ref.to != offset && tokenMatchesXrefTarget(token, ref)) {
            return ref.to;
        }
    }
    for (const XrefDescription &ref : refs) {
        if (ref.to != RVA_INVALID && ref.to != offset && xrefIsCall(ref)) {
            return ref.to;
        }
    }

    return RVA_INVALID;
}

bool DisassemblyWidget::isActionableTokenAt(const QPoint &pos)
{
    QTextCursor cursor = mDisasTextEdit->cursorForPosition(pos);
    const RVA offset = readDisassemblyOffset(cursor);
    if (offset == RVA_INVALID) {
        return false;
    }

    const QString token = tokenAtCursor(cursor);
    if (token.trimmed().isEmpty()) {
        return false;
    }

    return xrefTargetForToken(offset, token) != RVA_INVALID;
}

QString DisassemblyWidget::deepLinkAt(const QPoint &pos)
{
    const QTextCursor cursor = mDisasTextEdit->cursorForPosition(pos);
    return DeepLink::linkAt(cursor.block().text(), cursor.positionInBlock());
}

void DisassemblyWidget::updateDisassemblyCursor(const QPoint &pos, Qt::MouseButtons buttons)
{
    if (buttons & Qt::LeftButton) {
        mDisasTextEdit->viewport()->setCursor(Qt::IBeamCursor);
    } else if (isActionableTokenAt(pos) || !deepLinkAt(pos).isEmpty()) {
        mDisasTextEdit->viewport()->setCursor(Qt::PointingHandCursor);
    } else {
        mDisasTextEdit->viewport()->setCursor(Qt::ArrowCursor);
    }
}

bool DisassemblyWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Resize && obj == mDisasTextEdit->viewport()) {
        QMetaObject::invokeMethod(this, [this]() { updateMaxLines(); }, Qt::QueuedConnection);
    } else if (event->type() == QEvent::MouseMove && obj == mDisasTextEdit->viewport()) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        updateDisassemblyCursor(mouseEvent->pos(), mouseEvent->buttons());
    } else if (event->type() == QEvent::MouseButtonRelease && obj == mDisasTextEdit->viewport()) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && !mDisasTextEdit->textCursor().hasSelection()) {
            const QString link = deepLinkAt(mouseEvent->pos());
            if (!link.isEmpty()) {
                DeepLink::handle(mainWindow, link);
                return true;
            }
        }
        updateDisassemblyCursor(mouseEvent->pos(), mouseEvent->buttons());
    } else if (event->type() == QEvent::Leave && obj == mDisasTextEdit->viewport()) {
        mDisasTextEdit->viewport()->setCursor(Qt::ArrowCursor);
    } else if (
        event->type() == QEvent::MouseButtonDblClick
        && (obj == mDisasTextEdit || obj == mDisasTextEdit->viewport())) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);

        const QTextCursor &cursor = mDisasTextEdit->cursorForPosition(
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            mouseEvent->position().toPoint()
#else
            QPoint(mouseEvent->x(), mouseEvent->y())
#endif
        );
        jumpToOffsetUnderCursor(cursor);

        return true;
    } else if (event->type() == QEvent::ToolTip && obj == mDisasTextEdit->viewport()) {
        QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);

        QTextCursor cursorForWord = mDisasTextEdit->cursorForPosition(helpEvent->pos());
        const QString registerTooltip = PreviewTooltip::buildRegisterPreview(
            wordAtCursor(cursorForWord));
        if (!registerTooltip.isEmpty()) {
            QToolTip::showText(helpEvent->globalPos(), registerTooltip, this, QRect(), 3500);
            return true;
        }

        const QString token = tokenAtCursor(cursorForWord);
        RVA offsetFrom = readDisassemblyOffset(cursorForWord);
        RVA offsetTo = xrefTargetForToken(offsetFrom, token);

        // Only if the offset we point *to* is different from the one the
        // cursor is currently on *and* the former is a valid offset, we are
        // allowed to get a preview of offsetTo
        if (offsetTo != offsetFrom && offsetTo != RVA_INVALID) {
            const QString tooltip = PreviewTooltip::buildAddressPreview(offsetTo);
            if (!tooltip.isEmpty()) {
                QToolTip::showText(helpEvent->globalPos(), tooltip, this, QRect(), 3500);
            }
        }

        return true;
    }

    return MemoryDockWidget::eventFilter(obj, event);
}

void DisassemblyWidget::keyPressEvent(QKeyEvent *event)
{
    if (ShortcutMgr()->matches("marks.set", event)) {
        ShortcutKeysDialog dlg(ShortcutKeysDialog::SetMark, seekable->getOffset(), this);
        dlg.exec();
        return;
    } else if (ShortcutMgr()->matches("marks.jump", event)) {
        ShortcutKeysDialog dlg(ShortcutKeysDialog::JumpTo, RVA_INVALID, this);
        if (dlg.exec() == QDialog::Accepted) {
            QChar key = dlg.selectedKey();
            RVA addr = ShortcutKeys::instance()->getMark(key);
            seekable->seek(addr);
        }
        return;
    }
    if (ShortcutMgr()->matches("disasm.jumpToOffset", event)) {
        const QTextCursor cursor = mDisasTextEdit->textCursor();
        jumpToOffsetUnderCursor(cursor);
    }

    MemoryDockWidget::keyPressEvent(event);
}

QString DisassemblyWidget::getWindowTitle() const
{
    return tr("Disassembly");
}

void DisassemblyWidget::on_refreshContents()
{
    updateMaxLines();
}

void DisassemblyWidget::on_seekChanged(RVA offset)
{
    if (!seekFromCursor) {
        cursorLineOffset = 0;
        cursorCharOffset = 0;
    }

    if (topOffset != RVA_INVALID && offset >= topOffset && offset <= bottomOffset) {
        // if the line with the seek offset is currently visible, just move the
        // cursor there
        updateCursorPosition();
    } else {
        // otherwise scroll there
        refreshDisasm(offset);
    }
    mCtxMenu->setOffset(offset);
}

void DisassemblyWidget::fontsUpdatedSlot()
{
    setupFonts();

    if (!updateMaxLines()) { // updateMaxLines() returns true if it already
                             // refreshed.
        refreshDisasm();
    }
}

void DisassemblyWidget::colorsUpdatedSlot()
{
    PreviewTooltip::applyStyleSheet(this);
    setupColors();
    // Skip the expensive refreshDisasm() (which re-runs disassembleLines)
    // when only the interface palette changed; the cached line HTML still
    // carries valid radare2 colors, so a repaint is enough.
    if (Config()->isInterfacePaletteOnlyUpdate()) {
        mDisasTextEdit->viewport()->update();
        return;
    }
    clearBasicBlockColorCache();
    refreshDisasm();
}

void DisassemblyWidget::setupFonts()
{
    const QFont font = Config()->getFont();
    mDisasTextEdit->setFont(font);
    mDisasTextEdit->document()->setDefaultFont(font);
}

void DisassemblyWidget::setupColors()
{
    mDisasTextEdit->setBackgroundColor(ConfigColor("gui.background"));
    mDisasTextEdit->setStyleSheet(
        QStringLiteral(
            "QPlainTextEdit { background-color: %1; color: %2; border: 0px transparent black; }")
            .arg(ConfigColor("gui.background").name())
            .arg(ConfigColor("btext").name()));
}

qreal DisassemblyTextEdit::blockTop(const QTextBlock &block) const
{
    return blockBoundingGeometry(block).top();
}

// Viewport rectangles of every block, in document order
QList<QRectF> DisassemblyTextEdit::blockRects() const
{
    QList<QRectF> rects;
    const QPointF offset = contentOffset();
    for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
        rects.append(blockBoundingGeometry(block).translated(offset));
    }
    return rects;
}

qreal DisassemblyTextEdit::textOffset() const
{
    return (blockBoundingGeometry(document()->begin()).topLeft() + contentOffset()).y();
}

bool DisassemblyTextEdit::viewportEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::Type::Wheel: {
        QWheelEvent *wheelEvent = static_cast<QWheelEvent *>(event);
        if (wheelEvent->modifiers() & Qt::ControlModifier) {
            // zoom text
            int delta = wheelEvent->angleDelta().y();
            qreal zoomFactor = Config()->getZoomFactor();
            zoomFactor += delta > 0 ? 0.1 : -0.1;
            Config()->setZoomFactor(zoomFactor);
            emit refreshContents();
            event->accept();
            return true;
        }
        if (qAbs(wheelEvent->angleDelta().x()) > qAbs(wheelEvent->angleDelta().y())) {
            break; // let QPlainTextEdit scroll horizontally
        }
        disas->wheelScroll(wheelEvent);
        event->accept();
        return true;
    }
    default:
        break;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void DisassemblyTextEdit::scrollContentsBy(int dx, int dy)
{
    if (!lockScroll) {
        QPlainTextEdit::scrollContentsBy(dx, dy);
    }
}

void DisassemblyTextEdit::keyPressEvent(QKeyEvent *event)
{
    Q_UNUSED(event)
    // QPlainTextEdit::keyPressEvent(event);
}

void DisassemblyTextEdit::mousePressEvent(QMouseEvent *event)
{
    QPlainTextEdit::mousePressEvent(event);

    if (event->button() == Qt::RightButton && !textCursor().hasSelection()) {
        setTextCursor(cursorForPosition(event->pos()));
    }
}

void DisassemblyTextEdit::paintEvent(QPaintEvent *event)
{
    // The viewport is opaque so scrolling can blit it, paint its background
    QPainter(viewport()).fillRect(event->rect(), backgroundColor);
    QPlainTextEdit::paintEvent(event);

    if (!Config()->getConfigBool("asm.lines.fcn")) {
        return;
    }

    QList<DisassemblyLine> lines = disas->getLines();
    if (lines.isEmpty()) {
        return;
    }

    QPainter p(viewport());
    p.setPen(QPen(ConfigColor("flow"), 2, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));

    const int x = 2;
    struct RowSpan
    {
        qreal top;
        qreal lastTop;
        qreal bottom;
    };
    QMap<RVA, RowSpan> rowSpan;
    const QList<QRectF> rows = blockRects();
    for (int i = 0; i < rows.size() && i < lines.size(); i++) {
        const QRectF &r = rows.at(i);
        auto it = rowSpan.find(lines[i].offset);
        if (it == rowSpan.end()) {
            rowSpan.insert(lines[i].offset, {r.top(), r.top(), r.bottom()});
        } else {
            it->lastTop = r.top();
            it->bottom = r.bottom();
        }
    }
    if (rowSpan.isEmpty()) {
        return;
    }

    // Endpoints only depend on visible rows so blitted lines match a fresh paint
    const auto &functionRanges = disas->getFunctionRanges();
    for (auto range = functionRanges.cbegin(); range != functionRanges.cend(); ++range) {
        auto firstLine = rowSpan.lowerBound(range.key());
        if (firstLine == rowSpan.cend() || firstLine.key() >= range.value()) {
            continue;
        }
        const RowSpan &first = firstLine.value();
        const qreal startY = firstLine.key() != range.key() ? first.top
                                                            : (first.lastTop + first.bottom) / 2;
        auto lineAfterFunction = rowSpan.lowerBound(range.value());
        const qreal endY = lineAfterFunction == rowSpan.cend() ? rowSpan.last().bottom
                                                               : lineAfterFunction.value().top;
        p.drawLine(QPointF(x, startY), QPointF(x, endY));
    }
}

void DisassemblyWidget::seekPrev()
{
    Core()->seekPrev();
}

/*********************
 * Left panel
 *********************/

struct Range
{
    Range(RVA v1, RVA v2)
        : from(v1)
        , to(v2)
    {
        if (from > to)
            std::swap(from, to);
    }
    RVA from;
    RVA to;

    inline bool contains(const Range &other) const { return from <= other.from && to >= other.to; }

    inline bool contains(RVA point) const { return from <= point && to >= point; }
};

DisassemblyLeftPanel::DisassemblyLeftPanel(DisassemblyWidget *disas)
{
    this->disas = disas;
}

void DisassemblyLeftPanel::wheelEvent(QWheelEvent *event)
{
    this->disas->wheelScroll(event);
}

void DisassemblyLeftPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    using namespace std;
    constexpr int penSizePix = 1;
    constexpr int selectedPenSizePix = 4;
    constexpr int baseArrowWidth = 5;
    const int arrowWidth = qMax(1, qRound(baseArrowWidth * Config()->getZoomFactor()));
    int rightOffset = size().rwidth();
    auto tEdit = qobject_cast<DisassemblyTextEdit *>(disas->getTextWidget());
    QColor arrowColorDown = ConfigColor("flow");
    QColor arrowColorUp = ConfigColor("cflow");
    QPainter p(this);
    QPen penDown(arrowColorDown, penSizePix, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin);
    QPen penUp(arrowColorUp, penSizePix, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin);
    // Fill background
    p.fillRect(event->rect(), Config()->getColor("gui.background").darker(115));

    QList<DisassemblyLine> lines = disas->getLines();
    const QList<QRectF> rows = tEdit->blockRects();

    QMap<RVA, int> linesPixPosition;
    QMap<RVA, pair<RVA, int>> arrowInfo; /* offset -> (arrow, layer of arrow) */
    for (int i = 0; i < lines.size() && i < rows.size(); i++) {
        const auto &line = lines.at(i);
        linesPixPosition[line.offset] = qRound(rows.at(i).center().y());
        if (line.arrow != RVA_INVALID) {
            arrowInfo.insert(line.offset, {line.arrow, -1});
        }
    }

    for (auto it = arrowInfo.begin(); it != arrowInfo.end(); it++) {
        Range currRange = {it.key(), it.value().first};
        it.value().second = it.value().second == -1 ? 1 : it.value().second;
        for (auto innerIt = arrowInfo.begin(); innerIt != arrowInfo.end(); innerIt++) {
            if (innerIt == it) {
                continue;
            }
            Range innerRange = {innerIt.key(), innerIt.value().first};
            if (currRange.contains(innerRange) || currRange.contains(innerRange.from)) {
                it.value().second++;
            }
        }
    }

    // I'm sorry this loop below, but it is only way I see how to implement the
    // feature
    while (true) {
        bool correction = false;
        bool correction2 = false;
        for (auto it = arrowInfo.begin(); it != arrowInfo.end(); it++) {
            int minDistance = INT32_MAX;
            Range currRange = {it.key(), it.value().first};
            for (auto innerIt = arrowInfo.begin(); innerIt != arrowInfo.end(); innerIt++) {
                if (innerIt == it) {
                    continue;
                }
                Range innerRange = {innerIt.key(), innerIt.value().first};
                if (it.value().second == innerIt.value().second
                    && (currRange.contains(innerRange) || currRange.contains(innerRange.from))) {
                    it.value().second++;
                    correction = true;
                }
                int distance = it.value().second - innerIt.value().second;
                if (distance > 0 && distance < minDistance) {
                    minDistance = distance;
                }
            }
            if (minDistance > 1 && minDistance != INT32_MAX) {
                correction2 = true;
                it.value().second -= minDistance - 1;
            }
        }
        if (!correction && !correction2) {
            break;
        }
    }

    int maxArrowLayer = 0;
    for (auto it = arrowInfo.cbegin(); it != arrowInfo.cend(); ++it) {
        maxArrowLayer = qMax(maxArrowLayer, it.value().second);
    }
    const int configuredSpacing = Config()->getDisassemblyJumpLineSpacing();
    const qreal availableSpacing = qreal(qMax(0, rightOffset - penSizePix))
                                   / qMax(1, maxArrowLayer + 1);
    const qreal distanceBetweenLines = Config()->getDisassemblyJumpLineAdaptiveSpacingEnabled()
                                           ? qMin(qreal(configuredSpacing), availableSpacing)
                                           : configuredSpacing;

    const RVA currOffset = disas->getSeekable()->getOffset();
    // Draw the lines
    for (const auto &l : lines) {
        // Skip until we reach a line that jumps to a destination
        if (l.arrow == RVA_INVALID) {
            continue;
        }
        const int lineOffset = qRound(distanceBetweenLines * (arrowInfo.value(l.offset).second + 1));

        bool jumpDown = l.arrow > l.offset;
        p.setPen(jumpDown ? penDown : penUp);
        const bool isSelectedBranch = l.offset == currOffset || l.arrow == currOffset;
        if (isSelectedBranch) {
            QPen pen = p.pen();
            const QColor color = pen.color();
            pen.setColor(QColor(
                (color.red() + 255) / 2,
                (color.green() + 255) / 2,
                (color.blue() + 255) / 2,
                color.alpha()));
            pen.setWidth(selectedPenSizePix);
            p.setPen(pen);
        }
        bool endVisible = true;

        int currentLineYPos = linesPixPosition[l.offset];
        int lineArrowY = linesPixPosition.value(l.arrow, -1);

        if (lineArrowY == -1) {
            lineArrowY = jumpDown ? geometry().bottom() : 0;
            endVisible = false;
        }

        QPainterPath branchPath;
        branchPath.moveTo(rightOffset, currentLineYPos);
        branchPath.lineTo(rightOffset - lineOffset, currentLineYPos);
        branchPath.lineTo(rightOffset - lineOffset, lineArrowY);

        if (endVisible) {
            branchPath.lineTo(rightOffset - arrowWidth, lineArrowY);
        }
        p.setRenderHint(QPainter::Antialiasing, isSelectedBranch);
        p.drawPath(branchPath);

        if (endVisible) {
            QPainterPath arrow;
            arrow.moveTo(rightOffset - arrowWidth, lineArrowY + arrowWidth);
            arrow.lineTo(rightOffset - arrowWidth, lineArrowY - arrowWidth);
            arrow.lineTo(rightOffset, lineArrowY);
            p.fillPath(arrow, p.pen().brush());
        }
    }
}
