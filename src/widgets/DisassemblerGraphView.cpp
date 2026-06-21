#include "DisassemblerGraphView.h"
#include "common/BasicBlockHighlighter.h"
#include "common/BasicInstructionHighlighter.h"
#include "common/Colors.h"
#include "common/Configuration.h"
#include "common/Helpers.h"
#include "common/IaitoSeekable.h"
#include "common/PreviewTooltip.h"
#include "common/ShortcutManager.h"
#include "common/SyntaxHighlighter.h"
#include "common/TempConfig.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QShortcut>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolTip>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

#define TIMETORENDER 0
#include <chrono>

#define ADDR "addr"

static RVA addressFromBlockContentLine(const QString &line)
{
    static const QRegularExpression addressRegExp(QStringLiteral(R"(^[^\w]*(0x([0-9a-fA-F]+))\b)"));

    auto match = addressRegExp.match(line);
    if (!match.hasMatch()) {
        return RVA_INVALID;
    }

    bool ok = false;
    const RVA address = match.captured(2).toULongLong(&ok, 16);
    return ok ? address : RVA_INVALID;
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

static std::unordered_map<ut64, ut64> instructionSizesForBlock(
    const QJsonArray &opArray, RVA blockEntry, RVA blockSize)
{
    std::unordered_map<ut64, ut64> result;
    for (int opIndex = 0; opIndex < opArray.size(); opIndex++) {
        QJsonObject op = opArray[opIndex].toObject();
        RVA addr = op[ADDR].toVariant().toULongLong();
        RVA size = 0;

        if (opIndex < opArray.size() - 1) {
            RVA nextOffset = opArray[opIndex + 1].toObject()[ADDR].toVariant().toULongLong();
            size = nextOffset - addr;
        } else {
            size = (blockEntry + blockSize) - addr;
        }

        result[addr] = size;
    }
    return result;
}

DisassemblerGraphView::DisassemblerGraphView(
    QWidget *parent,
    IaitoSeekable *seekable,
    MainWindow *mainWindow,
    QList<QAction *> additionalMenuActions)
    : IaitoGraphView(parent)
    , blockMenu(new DisassemblyContextMenu(this, mainWindow))
    , contextMenu(new QMenu(this))
    , seekable(seekable)
    , actionUnhighlight(this)
    , actionUnhighlightInstruction(this)
{
    highlight_token = nullptr;
    auto *layout = new QVBoxLayout(this);
    // Signals that require a refresh all
    connect(Core(), &IaitoCore::refreshAll, this, &DisassemblerGraphView::refreshView);
    connect(Core(), &IaitoCore::commentsChanged, this, &DisassemblerGraphView::refreshView);
    connect(Core(), &IaitoCore::functionRenamed, this, &DisassemblerGraphView::refreshView);
    connect(Core(), &IaitoCore::flagsChanged, this, &DisassemblerGraphView::refreshView);
    connect(Core(), &IaitoCore::varsChanged, this, &DisassemblerGraphView::refreshView);
    connect(Core(), &IaitoCore::instructionChanged, this, &DisassemblerGraphView::refreshView);
    connect(Core(), &IaitoCore::breakpointsChanged, this, &DisassemblerGraphView::refreshView);
    connect(Core(), &IaitoCore::functionsChanged, this, &DisassemblerGraphView::refreshView);
    connect(Core(), &IaitoCore::asmOptionsChanged, this, &DisassemblerGraphView::refreshView);
    connect(Core(), &IaitoCore::refreshCodeViews, this, &DisassemblerGraphView::refreshView);
    connect(
        Core(),
        &IaitoCore::addressRangeSelectionChanged,
        this,
        &DisassemblerGraphView::applyAddressRangeSelection);

    connectSeekChanged(false);

    QShortcut *shortcut_escape
        = ShortcutMgr()->registerShortcut("graph.seekPrev", this, Qt::WidgetShortcut);
    connect(shortcut_escape, &QShortcut::activated, seekable, &IaitoSeekable::seekPrev);

    shortcuts.append(shortcut_escape);

    // Context menu that applies to everything
    auto *centerFocusedNodeAction = new QAction(tr("Center on focused node"), this);
    connect(
        centerFocusedNodeAction,
        &QAction::triggered,
        this,
        &DisassemblerGraphView::centerOnFocusedNode);
    contextMenu->addAction(centerFocusedNodeAction);
    contextMenu->addAction(&actionExportGraph);
    contextMenu->addMenu(layoutMenu);
    addBasicBlockContentMenu();
    if (mainWindow) {
        if (QAction *overviewAction = mainWindow->getOverviewAction()) {
            contextMenu->addAction(overviewAction);
        }
    }
    contextMenu->addSeparator();
    contextMenu->addActions(additionalMenuActions);

    QAction *highlightBB = new QAction(this);
    actionUnhighlight.setVisible(false);

    highlightBB->setText(tr("Highlight block"));
    connect(highlightBB, &QAction::triggered, this, [this]() {
        auto bbh = Core()->getBBHighlighter();
        RVA currBlockEntry = blockForAddress(this->seekable->getOffset())->entry;

        QColor background = disassemblyBackgroundColor;
        if (auto block = bbh->getBasicBlock(currBlockEntry)) {
            background = block->color;
        }

        QColor c
            = QColorDialog::getColor(background, this, QString(), QColorDialog::DontUseNativeDialog);
        if (c.isValid()) {
            bbh->highlight(currBlockEntry, c);
        }
        Config()->colorsUpdated();
    });

    actionUnhighlight.setText(tr("Unhighlight block"));
    connect(&actionUnhighlight, &QAction::triggered, this, [this]() {
        auto bbh = Core()->getBBHighlighter();
        bbh->clear(blockForAddress(this->seekable->getOffset())->entry);
        Config()->colorsUpdated();
    });

    QAction *highlightBI = new QAction(this);
    actionUnhighlightInstruction.setVisible(false);

    highlightBI->setText(tr("Highlight instruction"));
    connect(
        highlightBI,
        &QAction::triggered,
        this,
        &DisassemblerGraphView::onActionHighlightBITriggered);

    actionUnhighlightInstruction.setText(tr("Unhighlight instruction"));
    connect(
        &actionUnhighlightInstruction,
        &QAction::triggered,
        this,
        &DisassemblerGraphView::onActionUnhighlightBITriggered);

    blockMenu->addAction(highlightBB);
    blockMenu->addAction(&actionUnhighlight);
    blockMenu->addAction(highlightBI);
    blockMenu->addAction(&actionUnhighlightInstruction);

    // Include all actions from generic context menu in block specific menu
    blockMenu->addSeparator();
    blockMenu->addActions(contextMenu->actions());

    connect(blockMenu, &DisassemblyContextMenu::copy, this, &DisassemblerGraphView::copySelection);

    // Add header as widget to layout so it stretches to the layout width
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setAlignment(Qt::AlignTop);

    this->scale_thickness_multiplier = true;
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    PreviewTooltip::applyStyleSheet(this);
    connect(Config(), &Configuration::colorsUpdated, this, [this]() {
        PreviewTooltip::applyStyleSheet(this);
    });
}

void DisassemblerGraphView::connectSeekChanged(bool disconn)
{
    if (disconn) {
        disconnect(
            seekable,
            &IaitoSeekable::seekableSeekChanged,
            this,
            &DisassemblerGraphView::onSeekChanged);
    } else {
        connect(
            seekable,
            &IaitoSeekable::seekableSeekChanged,
            this,
            &DisassemblerGraphView::onSeekChanged);
    }
}

DisassemblerGraphView::~DisassemblerGraphView()
{
    for (QShortcut *shortcut : shortcuts) {
        delete shortcut;
    }
}

void DisassemblerGraphView::addBasicBlockContentMenu()
{
    auto *menu = new QMenu(tr("Basic block contents"), this);
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);

    auto addContentAction = [this, menu, group](const QString &name, BasicBlockContent content) {
        QAction *action = group->addAction(name);
        action->setCheckable(true);
        action->setData(static_cast<int>(content));
        action->setChecked(content == basicBlockContent);
        menu->addAction(action);
    };

    addContentAction(tr("Empty"), BasicBlockContent::Empty);
    addContentAction(tr("Disassembly (pdb)"), BasicBlockContent::Disassembly);
    addContentAction(tr("Comments (CCb)"), BasicBlockContent::Comments);
    addContentAction(tr("Strings (pdsb)"), BasicBlockContent::Strings);
    addContentAction(tr("Source lines (CLLb)"), BasicBlockContent::SourceLines);

    connect(group, &QActionGroup::triggered, this, [this](QAction *action) {
        setBasicBlockContent(static_cast<BasicBlockContent>(action->data().toInt()));
    });

    contextMenu->addMenu(menu);
}

void DisassemblerGraphView::setBasicBlockContent(BasicBlockContent content)
{
    if (basicBlockContent == content) {
        return;
    }

    basicBlockContent = content;
    refreshView();
}

QString DisassemblerGraphView::basicBlockContentCommand() const
{
    switch (basicBlockContent) {
    case BasicBlockContent::Empty:
        return QString();
    case BasicBlockContent::Disassembly:
        return QStringLiteral("pdb");
    case BasicBlockContent::Comments:
        return QStringLiteral("CCb");
    case BasicBlockContent::Strings:
        return QStringLiteral("pdsb");
    case BasicBlockContent::SourceLines:
        return QStringLiteral("CLLb");
    }

    return QStringLiteral("pdb");
}

void DisassemblerGraphView::refreshView()
{
    IaitoGraphView::refreshView();
    loadCurrentGraph();
    emit viewRefreshed();
}

void DisassemblerGraphView::loadCurrentGraph()
{
    TempConfig tempConfig;
    tempConfig.set("scr.color", COLOR_MODE_16M)
        .set("asm.lines", false)
        .set("asm.lines.bb", false)
        .set("asm.lines.fcn", false)
        .set("asm.trace.color", false);

    QJsonArray functions;
    RAnalFunction *fcn = Core()->functionIn(seekable->getOffset());
    if (fcn) {
        currentFcnAddr = fcn->addr;
        QJsonDocument functionsDoc = Core()->cmdj("agJ " + RAddressString(fcn->addr));
        functions = functionsDoc.array();
    }

    disassembly_blocks.clear();
    blocks.clear();
    outgoingXRefsCache.clear();

    if (highlight_token) {
        delete highlight_token;
        highlight_token = nullptr;
    }

    emptyGraph = functions.isEmpty();
    graphLoaded = true;
    if (emptyGraph) {
        // If there's no function to print, just add a message
        if (!emptyText) {
            emptyText = new QLabel(this);
            emptyText->setText(tr("No function detected. Cannot display graph."));
            emptyText->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
            layout()->addWidget(emptyText);
            layout()->setAlignment(emptyText, Qt::AlignHCenter);
        }
        emptyText->setVisible(true);
    } else if (emptyText) {
        emptyText->setVisible(false);
    }
    // Refresh global "empty graph" variable so other widget know there is
    // nothing to show here
    Core()->setGraphEmpty(emptyGraph);

    QJsonValue funcRef = functions.first();
    QJsonObject func = funcRef.toObject();

    windowTitle = tr("Graph");
    emit nameChanged(windowTitle);

    RVA entry = func[ADDR].toVariant().toULongLong();

    setEntry(entry);
    for (const QJsonValueRef value : func["blocks"].toArray()) {
        QJsonObject block = value.toObject();
        RVA block_entry = block[ADDR].toVariant().toULongLong();
        RVA block_size = block["size"].toVariant().toULongLong();
        RVA block_fail = block["fail"].toVariant().toULongLong();
        RVA block_jump = block["jump"].toVariant().toULongLong();

        DisassemblyBlock db;
        GraphBlock gb;
        gb.entry = block_entry;
        db.entry = block_entry;
        db.size = block_size;
        if (Config()->getGraphBlockEntryOffset()) {
            // QColor(0,0,0,0) is transparent
            db.header_text
                = Text("[" + RAddressString(db.entry) + "]", ConfigColor(ADDR), QColor(0, 0, 0, 0));
        }
        db.true_path = RVA_INVALID;
        db.false_path = RVA_INVALID;
        if (block_fail) {
            db.false_path = block_fail;
            gb.edges.emplace_back(block_fail);
        }
        if (block_jump) {
            if (block_fail) {
                db.true_path = block_jump;
            }
            gb.edges.emplace_back(block_jump);
        }

        QJsonObject switchOp = block["switchop"].toObject();
        if (!switchOp.isEmpty()) {
            QJsonArray caseArray = switchOp["cases"].toArray();
            for (QJsonValue caseOpValue : caseArray) {
                QJsonObject caseOp = caseOpValue.toObject();
                bool ok;
                RVA caseJump = caseOp["jump"].toVariant().toULongLong(&ok);
                if (!ok) {
                    continue;
                }
                gb.edges.emplace_back(caseJump);
            }
        }

        QJsonArray opArray = block["ops"].toArray();
        appendCommandBlockContent(db, opArray, block_size);
        if (db.instrs.empty() && basicBlockContent == BasicBlockContent::Disassembly) {
            appendJsonDisassemblyBlockContent(db, opArray, block_entry, block_size);
        }
        disassembly_blocks[db.entry] = db;
        prepareGraphNode(gb);

        addBlock(gb);
    }
    cleanupEdges(blocks);

    if (!func["blocks"].toArray().isEmpty()) {
        computeGraphPlacement();
    }
}

DisassemblerGraphView::EdgeConfigurationMapping DisassemblerGraphView::getEdgeConfigurations()
{
    EdgeConfigurationMapping result;
    for (auto &block : blocks) {
        for (const auto &edge : block.second.edges) {
            result[{block.first, edge.target}]
                = edgeConfiguration(block.second, &blocks[edge.target], false);
        }
    }
    return result;
}

DisassemblerGraphView::MinimapBars DisassemblerGraphView::getMinimapBars()
{
    MinimapBars result;
    const qreal padding = 2 * charWidth;
    for (auto &blockIt : blocks) {
        GraphBlock &block = blockIt.second;
        auto dbIt = disassembly_blocks.find(block.entry);
        if (dbIt == disassembly_blocks.end()) {
            continue;
        }
        DisassemblyBlock &db = dbIt->second;
        std::vector<MinimapBar> bars;
        const qreal maxX = block.x + block.width - padding;
        const qreal barHeight = qMax<qreal>(1.0, charHeight * 0.6);
        int y = block.y + getTextOffset(0).y();
        auto addLine = [&](const RichTextPainter::List &line) {
            qreal cx = block.x + padding;
            const qreal barY = y + (charHeight - barHeight) / 2.0;
            for (const auto &segment : line) {
                const qreal segWidth = segment.text.length() * charWidth;
                if (!segment.text.trimmed().isEmpty()) {
                    const QColor color = (segment.flags == RichTextPainter::FlagColor
                                          || segment.flags == RichTextPainter::FlagAll)
                                             ? segment.textColor
                                             : graphNodeColor;
                    bars.push_back({QRectF(cx, barY, qMin(segWidth, maxX - cx), barHeight), color});
                }
                cx += segWidth;
                if (cx >= maxX) {
                    break;
                }
            }
        };
        const bool hasTitleBar = Config()->getGraphBlockEntryOffset()
                                 && !db.header_text.lines.empty();
        if (hasTitleBar) {
            y += int(db.header_text.lines.size()) * charHeight;
        } else {
            for (auto &line : db.header_text.lines) {
                addLine(line);
                y += charHeight;
            }
        }
        for (const Instr &instr : db.instrs) {
            for (auto &line : instr.text.lines) {
                addLine(line);
                y += charHeight;
            }
        }
        result[block.entry] = std::move(bars);
    }
    return result;
}

void DisassemblerGraphView::appendJsonDisassemblyBlockContent(
    DisassemblyBlock &db, const QJsonArray &opArray, RVA blockEntry, RVA blockSize)
{
    for (int opIndex = 0; opIndex < opArray.size(); opIndex++) {
        QJsonObject op = opArray[opIndex].toObject();
        RVA addr = op[ADDR].toVariant().toULongLong();
        RVA size = 0;

        if (opIndex < opArray.size() - 1) {
            RVA nextOffset = opArray[opIndex + 1].toObject()[ADDR].toVariant().toULongLong();
            size = nextOffset - addr;
        } else {
            size = (blockEntry + blockSize) - addr;
        }

        appendBlockContentLine(db, op["text"].toString(), addr, size);
    }
}

void DisassemblerGraphView::appendCommandBlockContent(
    DisassemblyBlock &db, const QJsonArray &opArray, RVA blockSize)
{
    const QString command = basicBlockContentCommand();
    if (command.isEmpty()) {
        return;
    }

    const QString output = Core()->cmdRawAt(command, db.entry);
    const auto opSizes = instructionSizesForBlock(opArray, db.entry, blockSize);

    for (const QString &line : output.split(QLatin1Char('\n'), IAITO_QT_SKIP_EMPTY_PARTS)) {
        QTextDocument textDoc;
        textDoc.setHtml(IaitoCore::ansiEscapeToHtml(line));
        const RVA lineAddr = addressFromBlockContentLine(textDoc.toPlainText());
        ut64 lineSize = 0;
        if (lineAddr != RVA_INVALID) {
            auto sizeIt = opSizes.find(lineAddr);
            lineSize = sizeIt != opSizes.end() ? sizeIt->second : 1;
        }

        appendBlockContentLine(db, line, lineAddr, lineSize);
    }
}

void DisassemblerGraphView::appendBlockContentLine(
    DisassemblyBlock &db, const QString &line, RVA addr, ut64 size)
{
    if (line.trimmed().isEmpty()) {
        return;
    }

    QTextDocument textDoc;
    textDoc.setHtml(IaitoCore::ansiEscapeToHtml(line));

    Instr instr;
    instr.addr = addr;
    instr.size = size;
    instr.plainText = textDoc.toPlainText();

    RichTextPainter::List richText = RichTextPainter::fromTextDocument(textDoc);

    bool cropped = false;
    int blockLength = Config()->getGraphBlockMaxChars() + Core()->getConfigb("asm.bytes") * 24
                      + Core()->getConfigb("asm.emu") * 10;
    instr.text = Text(RichTextPainter::cropped(richText, blockLength, "...", &cropped));
    if (cropped) {
        instr.fullText = richText;
    } else {
        instr.fullText = Text();
    }

    db.instrs.push_back(instr);
}

void DisassemblerGraphView::prepareGraphNode(GraphBlock &block)
{
    DisassemblyBlock &db = disassembly_blocks[block.entry];
    int width = 0;
    int height = 0;
    for (auto &line : db.header_text.lines) {
        int lw = 0;
        for (auto &part : line)
            lw += mFontMetrics->width(part.text);
        if (lw > width)
            width = lw;
        height += 1;
    }
    for (Instr &instr : db.instrs) {
        for (auto &line : instr.text.lines) {
            int lw = 0;
            for (auto &part : line)
                lw += mFontMetrics->width(part.text);
            if (lw > width)
                width = lw;
            height += 1;
        }
    }
    int extra = static_cast<int>(4 * charWidth + 4);
    block.width = static_cast<int>(width + extra + charWidth);
    block.height = (height * charHeight) + extra;
}

void DisassemblerGraphView::drawBlock(QPainter &p, GraphView::GraphBlock &block, bool interactive)
{
    QRectF blockRect(block.x, block.y, block.width, block.height);

    const qreal padding = 2 * charWidth;

    DisassemblyBlock &db = disassembly_blocks[block.entry];

    if (Config()->getGraphBlockShadow()) {
        QColor shadowColor;
        if (db.terminal) {
            shadowColor = retShadowColor;
        } else if (db.indirectcall) {
            shadowColor = indirectcallShadowColor;
        } else {
            shadowColor = QColor(0, 0, 0, 40);
        }
        const qreal shadowOffset = 5;
        const int blurSteps = 6;
        int baseAlpha = shadowColor.alpha();
        if (baseAlpha == 0 || baseAlpha == 255) {
            baseAlpha = 40;
        }
        const int layerAlpha = qMax(3, baseAlpha / blurSteps);
        p.setPen(Qt::NoPen);
        const QRectF base = blockRect.translated(shadowOffset, shadowOffset);
        const qreal cornerRadius = 6;
        for (int i = blurSteps - 1; i >= 0; --i) {
            QColor layer = shadowColor;
            layer.setAlpha(layerAlpha);
            p.setBrush(layer);
            const qreal grow = i;
            p.drawRoundedRect(
                base.adjusted(-grow, -grow, grow, grow), cornerRadius + grow, cornerRadius + grow);
        }
    }

    p.setPen(graphNodeColor);
    p.setBrush(disassemblyBackgroundColor);
    p.setFont(Config()->getFont());
    p.drawRect(blockRect);

    breakpoints = Core()->getBreakpointsAddresses();

    bool block_selected = false;
    RVA selected_instruction = RVA_INVALID;

    // Figure out if the current block is selected
    RVA addr = seekable->getOffset();
    RVA PCAddr = Core()->getProgramCounterValue();
    RVA bbAddr = UT64_MAX;
    for (const Instr &instr : db.instrs) {
        if (instr.contains(addr) && interactive) {
            block_selected = true;
            selected_instruction = instr.addr;
        }
        if (bbAddr == UT64_MAX && instr.addr != RVA_INVALID) {
            bbAddr = instr.addr;
        }

        // TODO: L219
    }
    if (bbAddr == UT64_MAX) {
        bbAddr = db.entry;
    }

    p.setPen(QPen(graphNodeColor, 1));

    if (block_selected) {
        p.setBrush(disassemblySelectedBackgroundColor);
    } else {
        p.setBrush(disassemblyBackgroundColor);
    }
    QColor bbColor;
    auto bbColorStr = Core()->cmd("abc @ " + QString::number(bbAddr));
    if (bbColorStr != nullptr && bbColorStr.length() > 1) {
        char *s = r_str_newf("#ff%s", bbColorStr.mid(1, 6).toStdString().c_str());
        bbColor = QColor(QString(s));
        free(s);
    }

    const bool hasTitleBar = Config()->getGraphBlockEntryOffset() && !db.header_text.lines.empty();

    // Draw basic block body with the default background.
    p.drawRect(blockRect);
    auto bb = Core()->getBBHighlighter()->getBasicBlock(block.entry);
    if (bb) {
        QColor color(bb->color);
        p.setBrush(color);
        p.drawRect(blockRect);
    }

    // Apply the user-picked bb color as a subtle gradient tint fading from
    // the top-right corner. Non-intrusive hint — the body text stays
    // readable. The title bar will be drawn on top with a neutral color.
    if (bbColor.isValid()) {
        const qreal tintRadius = qMin<qreal>(block.width, block.height) * 0.8;
        QLinearGradient grad(
            block.x + block.width,
            block.y,
            block.x + block.width - tintRadius,
            block.y + tintRadius);
        QColor tintStart = bbColor;
        tintStart.setAlpha(90);
        QColor tintEnd = bbColor;
        tintEnd.setAlpha(0);
        grad.setColorAt(0.0, tintStart);
        grad.setColorAt(1.0, tintEnd);
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawRect(blockRect);
    }

    if (hasTitleBar) {
        const qreal titleBorder = 2;
        const qreal titleHeight = charHeight + 2 * titleBorder;
        const QRectF titleRect(block.x, block.y, block.width, titleHeight);
        const QColor titleBg = disassemblyBackgroundColor.darker(140);
        p.setPen(Qt::NoPen);
        p.setBrush(titleBg);
        p.drawRect(titleRect);

        p.setPen(QPen(graphNodeColor, 1));
        p.drawLine(
            QPointF(block.x, block.y + titleHeight),
            QPointF(block.x + block.width, block.y + titleHeight));

        // Title text in a color that contrasts with the title background so
        // it stays readable for any bb color; bold for emphasis.
        const int luma = (titleBg.red() * 299 + titleBg.green() * 587 + titleBg.blue() * 114)
                         / 1000;
        const QColor titleTextColor = (luma > 128) ? QColor(Qt::black) : QColor(Qt::white);
        const QString titleText = "[" + RAddressString(db.entry) + "]";
        QFont titleFont = Config()->getFont();
        titleFont.setBold(true);
        p.setPen(titleTextColor);
        p.setFont(titleFont);
        p.drawText(
            QRectF(
                block.x + charWidth, block.y + titleBorder, block.width - 2 * charWidth, charHeight),
            Qt::AlignVCenter | Qt::AlignLeft,
            titleText);
        p.setFont(Config()->getFont());
    }

    const int firstInstructionY = block.y + getInstructionOffset(db, 0).y();

    auto transform = p.combinedTransform();
    QRect screenChar = transform.mapRect(QRect(0, 0, charWidth, charHeight));
    const int screenCharWidth = screenChar.width() * qhelpers::devicePixelRatio(p.device());

    if (screenCharWidth < 4) {
        if (screenCharWidth < 1) {
            return;
        }
        const qreal maxX = block.x + block.width - padding;
        const qreal barHeight = qMax<qreal>(1.0, charHeight * 0.6);
        auto paintMinimapLine = [&](const RichTextPainter::List &line, int lineY) {
            qreal cx = block.x + padding;
            const qreal barY = lineY + (charHeight - barHeight) / 2.0;
            for (const auto &segment : line) {
                const qreal segWidth = segment.text.length() * charWidth;
                if (!segment.text.trimmed().isEmpty()) {
                    const QColor color = (segment.flags == RichTextPainter::FlagColor
                                          || segment.flags == RichTextPainter::FlagAll)
                                             ? segment.textColor
                                             : graphNodeColor;
                    p.fillRect(QRectF(cx, barY, qMin(segWidth, maxX - cx), barHeight), color);
                }
                cx += segWidth;
                if (cx >= maxX) {
                    break;
                }
            }
        };

        p.setPen(Qt::NoPen);
        int minimapY = block.y + getTextOffset(0).y();
        if (hasTitleBar) {
            minimapY += int(db.header_text.lines.size()) * charHeight;
        } else {
            for (auto &line : db.header_text.lines) {
                paintMinimapLine(line, minimapY);
                minimapY += charHeight;
            }
        }
        for (const Instr &instr : db.instrs) {
            for (auto &line : instr.text.lines) {
                paintMinimapLine(line, minimapY);
                minimapY += charHeight;
            }
        }
        return;
    }

    // Highlight selected tokens
    if (interactive && highlight_token != nullptr) {
        int y = firstInstructionY;
        qreal tokenWidth = mFontMetrics->width(highlight_token->content);

        for (const Instr &instr : db.instrs) {
            int pos = -1;

            while ((pos = instr.plainText.indexOf(highlight_token->content, pos + 1)) != -1) {
                int tokenEnd = pos + highlight_token->content.length();

                if ((pos > 0 && instr.plainText[pos - 1].isLetterOrNumber())
                    || (tokenEnd < instr.plainText.length()
                        && instr.plainText[tokenEnd].isLetterOrNumber())) {
                    continue;
                }

                qreal widthBefore = mFontMetrics->width(instr.plainText.left(pos));
                if (charWidth * 3 + widthBefore > block.width - (10 + padding)) {
                    continue;
                }

                qreal highlightWidth = tokenWidth;
                if (charWidth * 3 + widthBefore + tokenWidth >= block.width - (10 + padding)) {
                    highlightWidth = block.width - widthBefore - (10 + 2 * padding);
                }

                QColor selectionColor = ConfigColor("wordHighlight");

                p.fillRect(
                    QRectF(block.x + charWidth * 3 + widthBefore, y, highlightWidth, charHeight),
                    selectionColor);
            }

            y += int(instr.text.lines.size()) * charHeight;
        }
    }

    // Render node text
    auto x = block.x + padding;
    int y = block.y + getTextOffset(0).y();
    if (hasTitleBar) {
        // The title text was already drawn inside the title bar above.
        y += int(db.header_text.lines.size()) * charHeight;
    } else {
        for (auto &line : db.header_text.lines) {
            RichTextPainter::paintRichText<qreal>(
                &p, x, y, block.width, charHeight, 0, line, mFontMetrics.get());
            y += charHeight;
        }
    }

    auto bih = Core()->getBIHighlighter();
    for (const Instr &instr : db.instrs) {
        const QRect instrRect = QRect(
            static_cast<int>(block.x + charWidth),
            y,
            static_cast<int>(block.width - (10 + padding)),
            int(instr.text.lines.size()) * charHeight);

        QColor instrColor;
        if (instr.addr != RVA_INVALID && Core()->isBreakpoint(breakpoints, instr.addr)) {
            instrColor = ConfigColor("gui.breakpoint_background");
        } else if (instr.addr != RVA_INVALID && instr.addr == PCAddr) {
            instrColor = PCSelectionColor;
        } else if (instr.addr != RVA_INVALID) {
            if (auto background = bih->getBasicInstruction(instr.addr)) {
                instrColor = background->color;
            }
        }

        if (instrColor.isValid()) {
            p.fillRect(instrRect, instrColor);
        }

        if (selected_instruction != RVA_INVALID && selected_instruction == instr.addr) {
            p.fillRect(instrRect, disassemblySelectionColor);
        }
        if (instrOverlapsAddressRange(instr)) {
            p.fillRect(instrRect, disassemblySelectionColor);
        }

        for (auto &line : instr.text.lines) {
            int rectSize = qRound(charWidth);
            if (rectSize % 2) {
                rectSize++;
            }
            // Assume charWidth <= charHeight
            // TODO: Breakpoint/Cip stuff
            QRectF bpRect(x - rectSize / 3.0, y + (charHeight - rectSize) / 2.0, rectSize, rectSize);
            Q_UNUSED(bpRect);

            RichTextPainter::paintRichText<qreal>(
                &p,
                x + charWidth,
                y,
                block.width - charWidth,
                charHeight,
                0,
                line,
                mFontMetrics.get());
            y += charHeight;
        }
    }
}

GraphView::EdgeConfiguration DisassemblerGraphView::edgeConfiguration(
    GraphView::GraphBlock &from, GraphView::GraphBlock *to, bool interactive)
{
    EdgeConfiguration ec;
    DisassemblyBlock &db = disassembly_blocks[from.entry];
    if (to->entry == db.true_path) {
        ec.color = brtrueColor;
    } else if (to->entry == db.false_path) {
        ec.color = brfalseColor;
    } else {
        ec.color = jmpColor;
    }
    ec.start_arrow = false;
    ec.end_arrow = true;
    if (interactive) {
        if (from.entry == currentBlockAddress) {
            ec.width_scale = 2.0;
        } else if (to->entry == currentBlockAddress) {
            ec.width_scale = 2.0;
        }
    }
    return ec;
}

RVA DisassemblerGraphView::getAddrForMouseEvent(GraphBlock &block, QPoint *point)
{
    DisassemblyBlock &db = disassembly_blocks[block.entry];

    // Remove header and margin
    int off_y = getInstructionOffset(db, 0).y();

    // Get mouse coordinate over the actual text
    int text_point_y = point->y() - off_y;
    int mouse_row = text_point_y / charHeight;

    if (db.instrs.empty()) {
        return db.entry;
    }

    // If mouse coordinate is in header region or margin above
    if (mouse_row < 0) {
        return db.entry;
    }

    Instr *instr = getInstrForMouseEvent(block, point);
    if (instr) {
        return instr->addr == RVA_INVALID ? db.entry : instr->addr;
    }

    return RVA_INVALID;
}

DisassemblerGraphView::Instr *DisassemblerGraphView::getInstrForMouseEvent(
    GraphView::GraphBlock &block, QPoint *point, bool force)
{
    DisassemblyBlock &db = disassembly_blocks[block.entry];

    // Remove header and margin
    int off_y = getInstructionOffset(db, 0).y();

    // Get mouse coordinate over the actual text
    int text_point_y = point->y() - off_y;
    int mouse_row = text_point_y / charHeight;

    // Row in actual text
    int cur_row = 0;

    for (Instr &instr : db.instrs) {
        if (mouse_row < cur_row + (int) instr.text.lines.size()) {
            return &instr;
        }
        cur_row += instr.text.lines.size();
    }
    if (force && !db.instrs.empty()) {
        if (mouse_row <= 0) {
            return &db.instrs.front();
        } else {
            return &db.instrs.back();
        }
    }

    return nullptr;
}

QRectF DisassemblerGraphView::getInstrRect(GraphView::GraphBlock &block, RVA addr) const
{
    auto blockIt = disassembly_blocks.find(block.entry);
    if (blockIt == disassembly_blocks.end()) {
        return QRectF();
    }
    auto &db = blockIt->second;
    if (db.instrs.empty()) {
        return QRectF();
    }

    size_t sequenceAddr = db.instrs[0].addr;
    size_t firstLineWithAddr = 0;
    size_t currentLine = 0;
    for (size_t i = 0; i < db.instrs.size(); i++) {
        auto &instr = db.instrs[i];
        if (instr.addr != sequenceAddr) {
            sequenceAddr = instr.addr;
            firstLineWithAddr = currentLine;
        }
        if (instr.contains(addr)) {
            while (i < db.instrs.size() && db.instrs[i].addr == sequenceAddr) {
                currentLine += db.instrs[i].text.lines.size();
                i++;
            }
            QPointF topLeft = getInstructionOffset(db, static_cast<int>(firstLineWithAddr));
            return QRectF(
                topLeft,
                QSizeF(
                    block.width - 4 * charWidth, charHeight * int(currentLine - firstLineWithAddr)));
        }
        currentLine += instr.text.lines.size();
    }
    return QRectF();
}

void DisassemblerGraphView::showInstruction(GraphView::GraphBlock &block, RVA addr)
{
    QRectF rect = getInstrRect(block, addr);
    if (rect.isNull()) {
        return;
    }
    rect.translate(block.x, block.y);
    showRectangle(QRect(rect.x(), rect.y(), rect.width(), rect.height()), true);
}

DisassemblerGraphView::DisassemblyBlock *DisassemblerGraphView::blockForAddress(RVA addr)
{
    for (auto &blockIt : disassembly_blocks) {
        DisassemblyBlock &db = blockIt.second;
        for (const Instr &i : db.instrs) {
            if (i.addr == RVA_INVALID || i.size == RVA_INVALID) {
                continue;
            }

            if (i.contains(addr)) {
                return &db;
            }
        }
        if (db.entry != RVA_INVALID
            && ((db.size > 0 && db.entry <= addr && (addr - db.entry) < db.size)
                || (db.size == 0 && db.entry == addr))) {
            return &db;
        }
    }
    return nullptr;
}

const DisassemblerGraphView::Instr *DisassemblerGraphView::instrForAddress(RVA addr)
{
    DisassemblyBlock *block = blockForAddress(addr);
    if (!block) {
        return nullptr;
    }
    for (const Instr &i : block->instrs) {
        if (i.addr == RVA_INVALID || i.size == RVA_INVALID) {
            continue;
        }
        if (i.contains(addr)) {
            return &i;
        }
    }
    return nullptr;
}

RVA DisassemblerGraphView::instrEndAddress(const Instr &instr) const
{
    if (instr.addr == RVA_INVALID) {
        return RVA_INVALID;
    }
    if (instr.size == 0 || instr.size == RVA_INVALID) {
        return instr.addr;
    }
    if (instr.addr > std::numeric_limits<RVA>::max() - (instr.size - 1)) {
        return std::numeric_limits<RVA>::max();
    }
    return instr.addr + instr.size - 1;
}

bool DisassemblerGraphView::instrOverlapsAddressRange(const Instr &instr) const
{
    if (addressRangeSelectionStart == RVA_INVALID || addressRangeSelectionEnd == RVA_INVALID) {
        return false;
    }
    if (instr.addr == RVA_INVALID) {
        return false;
    }

    const RVA instrEnd = instrEndAddress(instr);
    return instrEnd != RVA_INVALID && instr.addr <= addressRangeSelectionEnd
           && instrEnd >= addressRangeSelectionStart;
}

void DisassemblerGraphView::extendAddressRangeSelection(const Instr &instr)
{
    if (instr.addr == RVA_INVALID || !Config()->getAddressRangeSelectionSyncEnabled()) {
        return;
    }

    RVA anchor = graphSelectionAnchor;
    if (anchor == RVA_INVALID && addressRangeSelectionStart != RVA_INVALID) {
        anchor = addressRangeSelectionStart;
    }
    if (anchor == RVA_INVALID) {
        anchor = seekable->getOffset();
    }

    const Instr *anchorInstr = instrForAddress(anchor);
    const RVA anchorStart = anchorInstr ? anchorInstr->addr : anchor;
    const RVA anchorEnd = anchorInstr ? instrEndAddress(*anchorInstr) : anchorStart;
    const RVA instrStart = static_cast<RVA>(instr.addr);
    const RVA instrEnd = instrEndAddress(instr);
    if (anchorStart == RVA_INVALID || anchorEnd == RVA_INVALID || instrEnd == RVA_INVALID) {
        return;
    }

    Core()->setAddressRangeSelection(qMin(anchorStart, instrStart), qMax(anchorEnd, instrEnd));
    graphSelectionAnchor = anchorStart;
}

void DisassemblerGraphView::applyAddressRangeSelection(RVA start, RVA end)
{
    const bool hasRange = start != RVA_INVALID && end != RVA_INVALID && start <= end;

    if (!hasRange) {
        addressRangeSelectionStart = RVA_INVALID;
        addressRangeSelectionEnd = RVA_INVALID;
        graphSelectionAnchor = RVA_INVALID;
    } else {
        addressRangeSelectionStart = start;
        addressRangeSelectionEnd = end;
        graphSelectionAnchor = start;
    }
    viewport()->update();
}

void DisassemblerGraphView::onSeekChanged(RVA addr)
{
    blockMenu->setOffset(addr);
    DisassemblyBlock *db = blockForAddress(addr);
    bool switchFunction = false;
    if (!db) {
        // not in this function, try refreshing
        refreshView();
        db = blockForAddress(addr);
        switchFunction = true;
    }
    if (db) {
        // This is a local address! We animated to it.
        transition_dont_seek = true;
        showBlock(blocks[db->entry], !switchFunction);
        if (instrForAddress(addr)) {
            showInstruction(blocks[db->entry], addr);
        }
    }
}

void DisassemblerGraphView::takeTrue()
{
    DisassemblyBlock *db = blockForAddress(seekable->getOffset());
    if (!db) {
        return;
    }

    if (db->true_path != RVA_INVALID) {
        seekable->seek(db->true_path);
    } else if (!blocks[db->entry].edges.empty()) {
        seekable->seek(blocks[db->entry].edges[0].target);
    }
}

void DisassemblerGraphView::takeFalse()
{
    DisassemblyBlock *db = blockForAddress(seekable->getOffset());
    if (!db) {
        return;
    }

    if (db->false_path != RVA_INVALID) {
        seekable->seek(db->false_path);
    } else if (!blocks[db->entry].edges.empty()) {
        seekable->seek(blocks[db->entry].edges[0].target);
    }
}

void DisassemblerGraphView::seekInstruction(bool previous_instr)
{
    RVA addr = seekable->getOffset();
    DisassemblyBlock *db = blockForAddress(addr);
    if (!db) {
        return;
    }

    for (size_t i = 0; i < db->instrs.size(); i++) {
        Instr &instr = db->instrs[i];
        if (!instr.contains(addr)) {
            continue;
        }

        // Found the instruction. Check if a next one exists
        if (!previous_instr && (i < db->instrs.size() - 1)) {
            seekable->seek(db->instrs[i + 1].addr);
        } else if (previous_instr && (i > 0)) {
            while (i > 0 && db->instrs[i].addr == addr) { // jump over 0 size instructions
                i--;
            }
            seekable->seek(db->instrs[i].addr);
            break;
        }
    }
}

void DisassemblerGraphView::nextInstr()
{
    seekInstruction(false);
}

void DisassemblerGraphView::prevInstr()
{
    seekInstruction(true);
}

static bool isGraphNavKey(const QKeyEvent *event)
{
    return ShortcutMgr()->matches("graph.takeTrue", event)
           || ShortcutMgr()->matches("graph.takeFalse", event)
           || ShortcutMgr()->matches("graph.nextInstr", event)
           || ShortcutMgr()->matches("graph.prevInstr", event)
           || ShortcutMgr()->matches("graph.seekPrevBlock", event);
}

void DisassemblerGraphView::seekPrevBlock()
{
    DisassemblyBlock *db = blockForAddress(seekable->getOffset());
    if (!db) {
        return;
    }
    ut64 current = db->entry;
    for (const auto &entry : blocks) {
        for (const auto &edge : entry.second.edges) {
            if (edge.target == current) {
                seekable->seek(entry.first);
                return;
            }
        }
    }
}

bool DisassemblerGraphView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (isGraphNavKey(keyEvent)) {
            event->accept();
            return true;
        }
    }
    return IaitoGraphView::event(event);
}

void DisassemblerGraphView::keyPressEvent(QKeyEvent *event)
{
    if (ShortcutMgr()->matches("graph.takeTrue", event)) {
        takeTrue();
    } else if (ShortcutMgr()->matches("graph.takeFalse", event)) {
        takeFalse();
    } else if (ShortcutMgr()->matches("graph.nextInstr", event)) {
        nextInstr();
    } else if (ShortcutMgr()->matches("graph.prevInstr", event)) {
        prevInstr();
    } else if (ShortcutMgr()->matches("graph.seekPrevBlock", event)) {
        seekPrevBlock();
    } else {
        IaitoGraphView::keyPressEvent(event);
        return;
    }
    event->accept();
}

void DisassemblerGraphView::seekLocal(RVA addr, bool update_viewport)
{
    RVA curAddr = seekable->getOffset();
    if (addr == curAddr) {
        return;
    }
    connectSeekChanged(true);
    seekable->seek(addr);
    connectSeekChanged(false);
    if (update_viewport) {
        viewport()->update();
    }
}

void DisassemblerGraphView::copySelection()
{
    if (!highlight_token)
        return;

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(highlight_token->content);
}

void DisassemblerGraphView::centerOnFocusedNode()
{
    DisassemblyBlock *db = blockForAddress(seekable->getOffset());
    if (!db) {
        return;
    }
    auto blockIt = blocks.find(db->entry);
    if (blockIt != blocks.end()) {
        centerBlock(blockIt->second);
    }
}

DisassemblerGraphView::Token *DisassemblerGraphView::getToken(Instr *instr, int x)
{
    x -= (int) (3 * charWidth); // Ignore left margin
    if (x < 0) {
        return nullptr;
    }

    int clickedCharPos = mFontMetrics->position(instr->plainText, x);
    if (clickedCharPos > instr->plainText.length()) {
        return nullptr;
    }

    static const QRegularExpression tokenRegExp(R"(\b(?<!\.)([^\s]+)\b(?!\.))");
    QRegularExpressionMatchIterator i = tokenRegExp.globalMatch(instr->plainText);

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();

        if (match.capturedStart() <= clickedCharPos && match.capturedEnd() > clickedCharPos) {
            auto t = new Token;
            t->start = match.capturedStart();
            t->length = match.capturedLength();
            t->content = match.captured();
            t->instr = instr;

            return t;
        }
    }

    return nullptr;
}

QString DisassemblerGraphView::registerTooltipForTokenAt(Instr *instr, int x)
{
    for (const QString &token : tokensAt(instr, x)) {
        const QString registerTooltip = PreviewTooltip::buildRegisterPreview(token);
        if (!registerTooltip.isEmpty()) {
            return registerTooltip;
        }
    }

    return QString();
}

QStringList DisassemblerGraphView::tokensAt(Instr *instr, int x)
{
    const int charWidthInt = static_cast<int>(charWidth);
    const int offsets[] = {0, charWidthInt, -charWidthInt};
    QStringList result;

    for (int offset : offsets) {
        Token *token = getToken(instr, x + offset);
        if (!token) {
            continue;
        }

        const QString content = token->content;
        delete token;
        if (!content.isEmpty() && !result.contains(content)) {
            result << content;
        }
    }

    return result;
}

QList<XrefDescription> DisassemblerGraphView::getOutgoingXRefs(RVA offset)
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

RVA DisassemblerGraphView::xrefTargetForToken(Instr *instr, const QString &token)
{
    if (!instr || instr->addr == RVA_INVALID || normalizedActionToken(token).isEmpty()) {
        return RVA_INVALID;
    }

    const QList<XrefDescription> refs = getOutgoingXRefs(instr->addr);
    for (const XrefDescription &ref : refs) {
        if (ref.to != RVA_INVALID && ref.to != instr->addr && tokenMatchesXrefTarget(token, ref)) {
            return ref.to;
        }
    }
    for (const XrefDescription &ref : refs) {
        if (ref.to != RVA_INVALID && ref.to != instr->addr && xrefIsCall(ref)) {
            return ref.to;
        }
    }

    return RVA_INVALID;
}

RVA DisassemblerGraphView::xrefTargetForTokenAt(Instr *instr, int x)
{
    for (const QString &token : tokensAt(instr, x)) {
        const RVA target = xrefTargetForToken(instr, token);
        if (target != RVA_INVALID) {
            return target;
        }
    }

    return RVA_INVALID;
}

bool DisassemblerGraphView::isActionableTokenAt(GraphBlock &block, QPoint pos)
{
    Instr *instr = getInstrForMouseEvent(block, &pos);
    if (!instr) {
        return false;
    }

    return xrefTargetForTokenAt(instr, pos.x()) != RVA_INVALID;
}

void DisassemblerGraphView::updateGraphCursor(const QPoint &pos)
{
    QPoint logicalPos = viewToLogicalCoordinates(pos);
    GraphBlock *block = getBlockContaining(logicalPos);
    if (!block) {
        viewport()->setCursor(Qt::ArrowCursor);
        return;
    }

    QPoint blockPos = logicalPos - QPoint(block->x, block->y);
    viewport()->setCursor(
        isActionableTokenAt(*block, blockPos) ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

QPoint DisassemblerGraphView::getInstructionOffset(const DisassemblyBlock &block, int line) const
{
    return getTextOffset(line + static_cast<int>(block.header_text.lines.size()));
}

void DisassemblerGraphView::blockClicked(GraphView::GraphBlock &block, QMouseEvent *event, QPoint pos)
{
    Instr *instr = getInstrForMouseEvent(block, &pos, event->button() == Qt::RightButton);
    if (highlight_token) {
        delete highlight_token;
        highlight_token = nullptr;
    }

    if (!instr) {
        currentBlockAddress = block.entry;
        if (event->button() == Qt::LeftButton) {
            graphSelectionAnchor = block.entry;
            if (Config()->getAddressRangeSelectionSyncEnabled()) {
                Core()->clearAddressRangeSelection();
            }
        }
        seekLocal(block.entry);
        blockMenu->setOffset(block.entry);
        blockMenu->setCanCopy(false);
        viewport()->update();
        return;
    }

    currentBlockAddress = block.entry;

    highlight_token = getToken(instr, pos.x());

    RVA addr = instr->addr == RVA_INVALID ? block.entry : instr->addr;
    if (event->button() == Qt::LeftButton) {
        if (event->modifiers() & Qt::ShiftModifier) {
            extendAddressRangeSelection(*instr);
        } else {
            graphSelectionAnchor = addr;
            if (Config()->getAddressRangeSelectionSyncEnabled()) {
                Core()->clearAddressRangeSelection();
            }
        }
    }
    seekLocal(addr);

    blockMenu->setOffset(addr);
    blockMenu->setCanCopy(highlight_token);
    if (highlight_token) {
        blockMenu->setCurHighlightedWord(highlight_token->content);
    }
    viewport()->update();
}

void DisassemblerGraphView::mouseMoveEvent(QMouseEvent *event)
{
    IaitoGraphView::mouseMoveEvent(event);
    if (event->buttons() == Qt::NoButton) {
        updateGraphCursor(event->pos());
    }
}

void DisassemblerGraphView::mouseReleaseEvent(QMouseEvent *event)
{
    GraphView::mouseReleaseEvent(event);
    if (event->buttons() == Qt::NoButton) {
        updateGraphCursor(event->pos());
    }
}

void DisassemblerGraphView::blockContextMenuRequested(
    GraphView::GraphBlock &block, QContextMenuEvent *event, QPoint /*pos*/)
{
    currentBlockAddress = block.entry;
    const RVA offset = this->seekable->getOffset();
    DisassemblyBlock *currentBlock = blockForAddress(offset);
    const RVA menuOffset = currentBlock && currentBlock->entry == block.entry
                               ? offset
                               : static_cast<RVA>(block.entry);
    blockMenu->setOffset(menuOffset);
    actionUnhighlight.setVisible(Core()->getBBHighlighter()->getBasicBlock(block.entry));
    actionUnhighlightInstruction.setVisible(
        instrForAddress(menuOffset) && Core()->getBIHighlighter()->getBasicInstruction(menuOffset));
    event->accept();
    blockMenu->exec(event->globalPos());
}

void DisassemblerGraphView::contextMenuEvent(QContextMenuEvent *event)
{
    GraphView::contextMenuEvent(event);
    if (!event->isAccepted()) {
        // TODO: handle opening block menu using keyboard
        contextMenu->exec(event->globalPos());
        event->accept();
    }
}

void DisassemblerGraphView::showExportDialog()
{
    QString defaultName = "graph";
    if (auto f = Core()->functionIn(currentFcnAddr)) {
        QString functionName = f->name;
        // don't confuse image type guessing and make c++ names somewhat usable
        functionName.replace(QRegularExpression("[.:]"), "_");
        functionName.remove(QRegularExpression("[^a-zA-Z0-9_].*"));
        if (!functionName.isEmpty()) {
            defaultName = functionName;
        }
    }
    showExportGraphDialog(defaultName, "agf", currentFcnAddr);
}

void DisassemblerGraphView::blockDoubleClicked(
    GraphView::GraphBlock &block, QMouseEvent *event, QPoint pos)
{
    Q_UNUSED(event);
    seekable->seekToReference(getAddrForMouseEvent(block, &pos));
}

void DisassemblerGraphView::blockHelpEvent(
    GraphView::GraphBlock &block, QHelpEvent *event, QPoint pos)
{
    Instr *instr = getInstrForMouseEvent(block, &pos);
    if (!instr || instr->fullText.lines.empty()) {
        if (!instr) {
            QToolTip::hideText();
            event->ignore();
            return;
        }
    }

    const QString registerTooltip = registerTooltipForTokenAt(instr, pos.x());
    if (!registerTooltip.isEmpty()) {
        QToolTip::showText(event->globalPos(), registerTooltip, viewport());
        return;
    }

    const RVA offsetTo = xrefTargetForTokenAt(instr, pos.x());
    if (offsetTo != RVA_INVALID && offsetTo != instr->addr) {
        const QString tooltip = PreviewTooltip::buildAddressPreview(offsetTo);
        if (!tooltip.isEmpty()) {
            QToolTip::showText(event->globalPos(), tooltip, viewport());
            return;
        }
    }

    if (instr->fullText.lines.empty()) {
        QToolTip::hideText();
        event->ignore();
        return;
    }

    QToolTip::showText(event->globalPos(), instr->fullText.ToQString());
}

bool DisassemblerGraphView::helpEvent(QHelpEvent *event)
{
    if (!GraphView::helpEvent(event)) {
        QToolTip::hideText();
        event->ignore();
    }

    return true;
}

void DisassemblerGraphView::blockTransitionedTo(GraphView::GraphBlock *to)
{
    currentBlockAddress = to->entry;
    if (transition_dont_seek) {
        transition_dont_seek = false;
        return;
    }
    seekLocal(to->entry);
}

void DisassemblerGraphView::onActionHighlightBITriggered()
{
    const RVA offset = this->seekable->getOffset();
    const Instr *instr = instrForAddress(offset);

    if (!instr) {
        return;
    }

    auto bih = Core()->getBIHighlighter();
    QColor background = ConfigColor("linehl");
    if (auto currentColor = bih->getBasicInstruction(offset)) {
        background = currentColor->color;
    }

    QColor c
        = QColorDialog::getColor(background, this, QString(), QColorDialog::DontUseNativeDialog);
    if (c.isValid()) {
        bih->highlight(instr->addr, instr->size, c);
    }
    Config()->colorsUpdated();
}

void DisassemblerGraphView::onActionUnhighlightBITriggered()
{
    const RVA offset = this->seekable->getOffset();
    const Instr *instr = instrForAddress(offset);

    if (!instr) {
        return;
    }

    auto bih = Core()->getBIHighlighter();
    bih->clear(instr->addr, instr->size);
    Config()->colorsUpdated();
}

void DisassemblerGraphView::restoreCurrentBlock()
{
    onSeekChanged(this->seekable->getOffset()); // try to keep the view on current block
}

void DisassemblerGraphView::paintEvent(QPaintEvent *event)
{
    // DisassemblerGraphView is always dirty
    setCacheDirty();

#if TIMETORENDER
    auto start = std::chrono::high_resolution_clock::now();
    GraphView::paintEvent(event);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    qDebug() << "Graph render time:" << duration.count() << "ms";
#else
    GraphView::paintEvent(event);
#endif
}

bool DisassemblerGraphView::Instr::contains(ut64 addr) const
{
    if (this->addr == RVA_INVALID || size == 0 || size == RVA_INVALID) {
        return false;
    }
    return this->addr <= addr && (addr - this->addr) < size;
}
