#include "DisassemblerGraphView.h"
#include "common/BasicBlockHighlighter.h"
#include "common/BasicInstructionHighlighter.h"
#include "common/Colors.h"
#include "common/Configuration.h"
#include "common/Helpers.h"
#include "common/IaitoSeekable.h"
#include "common/SyntaxHighlighter.h"
#include "common/TempConfig.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"

#include <QAction>
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

#define TIMETORENDER 0
#include <chrono>

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

    connectSeekChanged(false);

    // ESC for previous
    QShortcut *shortcut_escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    shortcut_escape->setContext(Qt::WidgetShortcut);
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

void DisassemblerGraphView::refreshView()
{
    IaitoGraphView::refreshView();
    loadCurrentGraph();
    emit viewRefreshed();
}

#define ADDR "addr"

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

    if (highlight_token) {
        delete highlight_token;
        highlight_token = nullptr;
    }

    emptyGraph = functions.isEmpty();
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
    QString funcName = func["name"].toString().trimmed();
    if (emptyGraph) {
        windowTitle += " (Empty)";
    } else if (!funcName.isEmpty()) {
        windowTitle += " (" + funcName + ")";
    }
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
        for (int opIndex = 0; opIndex < opArray.size(); opIndex++) {
            QJsonObject op = opArray[opIndex].toObject();
            Instr i;
            i.addr = op[ADDR].toVariant().toULongLong();

            if (opIndex < opArray.size() - 1) {
                // get instruction size from distance to next instruction ...
                RVA nextOffset = opArray[opIndex + 1].toObject()[ADDR].toVariant().toULongLong();
                i.size = nextOffset - i.addr;
            } else {
                // or to the end of the block.
                i.size = (block_entry + block_size) - i.addr;
            }

            QTextDocument textDoc;
            textDoc.setHtml(IaitoCore::ansiEscapeToHtml(op["text"].toString()));

            i.plainText = textDoc.toPlainText();

            RichTextPainter::List richText = RichTextPainter::fromTextDocument(textDoc);
            // Colors::colorizeAssembly(richText, textDoc.toPlainText(), 0);

            bool cropped;
            int blockLength = Config()->getGraphBlockMaxChars()
                              + Core()->getConfigb("asm.bytes") * 24
                              + Core()->getConfigb("asm.emu") * 10;
            i.text = Text(RichTextPainter::cropped(richText, blockLength, "...", &cropped));
            if (cropped)
                i.fullText = richText;
            else
                i.fullText = Text();
            db.instrs.push_back(i);
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
                base.adjusted(-grow, -grow, grow, grow),
                cornerRadius + grow,
                cornerRadius + grow);
        }
    }

    p.setPen(Qt::black);
    p.setBrush(Qt::gray);
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
        if (bbAddr == UT64_MAX) {
            bbAddr = instr.addr;
        }

        // TODO: L219
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

    const bool hasTitleBar = Config()->getGraphBlockEntryOffset()
                             && !db.header_text.lines.empty();

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
        const int luma
            = (titleBg.red() * 299 + titleBg.green() * 587 + titleBg.blue() * 114) / 1000;
        const QColor titleTextColor = (luma > 128) ? QColor(Qt::black) : QColor(Qt::white);
        const QString titleText = "[" + RAddressString(db.entry) + "]";
        QFont titleFont = Config()->getFont();
        titleFont.setBold(true);
        p.setPen(titleTextColor);
        p.setFont(titleFont);
        p.drawText(
            QRectF(
                block.x + charWidth,
                block.y + titleBorder,
                block.width - 2 * charWidth,
                charHeight),
            Qt::AlignVCenter | Qt::AlignLeft,
            titleText);
        p.setFont(Config()->getFont());
    }

    const int firstInstructionY = block.y + getInstructionOffset(db, 0).y();

    // Stop rendering text when it's too small
    auto transform = p.combinedTransform();
    QRect screenChar = transform.mapRect(QRect(0, 0, charWidth, charHeight));

    if (screenChar.width() * qhelpers::devicePixelRatio(p.device()) < 4) {
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
        if (Core()->isBreakpoint(breakpoints, instr.addr)) {
            instrColor = ConfigColor("gui.breakpoint_background");
        } else if (instr.addr == PCAddr) {
            instrColor = PCSelectionColor;
        } else if (auto background = bih->getBasicInstruction(instr.addr)) {
            instrColor = background->color;
        }

        if (instrColor.isValid()) {
            p.fillRect(instrRect, instrColor);
        }

        if (selected_instruction != RVA_INVALID && selected_instruction == instr.addr) {
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

    // If mouse coordinate is in header region or margin above
    if (mouse_row < 0) {
        return db.entry;
    }

    Instr *instr = getInstrForMouseEvent(block, point);
    if (instr) {
        return instr->addr;
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
        showInstruction(blocks[db->entry], addr);
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

static bool isGraphNavKey(int key, Qt::KeyboardModifiers mods)
{
    if (mods != Qt::NoModifier && mods != Qt::KeypadModifier) {
        return false;
    }
    switch (key) {
    case Qt::Key_T:
    case Qt::Key_F:
    case Qt::Key_J:
    case Qt::Key_K:
    case Qt::Key_U:
    case Qt::Key_Up:
    case Qt::Key_Down:
        return true;
    default:
        return false;
    }
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
        if (isGraphNavKey(keyEvent->key(), keyEvent->modifiers())) {
            event->accept();
            return true;
        }
    }
    return IaitoGraphView::event(event);
}

void DisassemblerGraphView::keyPressEvent(QKeyEvent *event)
{
    if (isGraphNavKey(event->key(), event->modifiers())) {
        switch (event->key()) {
        case Qt::Key_T:
            takeTrue();
            break;
        case Qt::Key_F:
            takeFalse();
            break;
        case Qt::Key_J:
        case Qt::Key_Down:
            nextInstr();
            break;
        case Qt::Key_K:
        case Qt::Key_Up:
            prevInstr();
            break;
        case Qt::Key_U:
            seekPrevBlock();
            break;
        }
        event->accept();
        return;
    }
    IaitoGraphView::keyPressEvent(event);
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

QPoint DisassemblerGraphView::getInstructionOffset(const DisassemblyBlock &block, int line) const
{
    return getTextOffset(line + static_cast<int>(block.header_text.lines.size()));
}

void DisassemblerGraphView::blockClicked(GraphView::GraphBlock &block, QMouseEvent *event, QPoint pos)
{
    Instr *instr = getInstrForMouseEvent(block, &pos, event->button() == Qt::RightButton);
    if (!instr) {
        return;
    }

    currentBlockAddress = block.entry;

    highlight_token = getToken(instr, pos.x());

    RVA addr = instr->addr;
    seekLocal(addr);

    blockMenu->setOffset(addr);
    blockMenu->setCanCopy(highlight_token);
    if (highlight_token) {
        blockMenu->setCurHighlightedWord(highlight_token->content);
    }
    viewport()->update();
}

void DisassemblerGraphView::blockContextMenuRequested(
    GraphView::GraphBlock &block, QContextMenuEvent *event, QPoint /*pos*/)
{
    const RVA offset = this->seekable->getOffset();
    actionUnhighlight.setVisible(Core()->getBBHighlighter()->getBasicBlock(block.entry));
    actionUnhighlightInstruction.setVisible(Core()->getBIHighlighter()->getBasicInstruction(offset));
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
    return this->addr <= addr && (addr - this->addr) < size;
}
