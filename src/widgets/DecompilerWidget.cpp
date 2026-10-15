#include "DecompilerWidget.h"
#include "common/ShortcutManager.h"
#include "menus/DecompilerContextMenu.h"
#include "ui_DecompilerWidget.h"

#include "common/Configuration.h"
#include "common/Decompiler.h"
#include "common/DecompilerHighlighter.h"
#include "common/DeepLink.h"
#include "common/Helpers.h"
#include "common/IaitoSeekable.h"
#include "common/SelectionHighlight.h"
#include "common/TempConfig.h"
#include "common/TextEditDialog.h"
#include "core/MainWindow.h"

#include "common/DecompileTask.h"
#include <QPushButton>

#include "dialogs/ShortcutKeysDialog.h"
#include <algorithm>
#include <QAbstractSlider>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QObject>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QTextBlock>
#include <QTextBlockUserData>
#include <QTextEdit>
#include <QTextLayout>
#include <QUrl>
#include <QWheelEvent>

DecompilerWidget::DecompilerWidget(MainWindow *main)
    : MemoryDockWidget(MemoryWidgetType::Decompiler, main)
    , mCtxMenu(new DecompilerContextMenu(this, main))
    , ui(new Ui::DecompilerWidget)
    , decompilerBusy(false)
    , seekFromCursor(false)
    , scrollerHorizontal(0)
    , scrollerVertical(0)
    , previousFunctionAddr(RVA_INVALID)
    , decompiledFunctionAddr(RVA_INVALID)
    , sourceCode(
          Decompiler::makeWarning(tr("Choose an offset and refresh to get decompiled code")),
          &r_codemeta_free)
    , code(r_codemeta_new(""), &r_codemeta_free)
{
    ui->setupUi(this);
    // Keep a keyboard cursor in the read-only decompilation listing.
    ui->textEdit->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setFocusProxy(ui->textEdit);
    setupSeekHistoryBar(true);
    showAddressColumn = Config()->getDecompilerShowAddresses();
    showDisassemblyColumn = Config()->getDecompilerShowDisassembly();
    setObjectName(main ? main->getUniqueObjectName(getWidgetType()) : getWidgetType());
    updateWindowTitle();

    setHighlighter(false);
    // Event filter to intercept key, mouse double click, and right click in the textbox
    ui->textEdit->viewport()->installEventFilter(this);
    ui->textEdit->installEventFilter(this);
    ui->textEdit->setCursor(Qt::ArrowCursor);
    ui->textEdit->viewport()->setCursor(Qt::ArrowCursor);
    ui->textEdit->setMouseTracking(true);
    ui->textEdit->viewport()->setMouseTracking(true);
    for (QScrollBar *scrollBar :
         {ui->textEdit->horizontalScrollBar(), ui->textEdit->verticalScrollBar()}) {
        connect(
            scrollBar,
            &QScrollBar::valueChanged,
            this,
            &DecompilerWidget::refreshDecompilerHover,
            Qt::QueuedConnection);
    }

    setupFonts();
    colorsUpdatedSlot();

    connect(Config(), &Configuration::fontsUpdated, this, &DecompilerWidget::fontsUpdatedSlot);
    connect(Config(), &Configuration::colorsUpdated, this, &DecompilerWidget::colorsUpdatedSlot);
    connect(Core(), &IaitoCore::registersChanged, this, &DecompilerWidget::highlightPC);
    connect(mCtxMenu, &DecompilerContextMenu::copy, this, &DecompilerWidget::copy);

    refreshDeferrer = createRefreshDeferrer([this]() { doRefresh(); });

    auto decompilers = Core()->getDecompilers();
    QString selectedDecompilerId = Config()->getSelectedDecompiler();
    if (selectedDecompilerId.isEmpty()) {
        // If no decompiler was previously chosen. set r2ghidra as default
        // decompiler
        selectedDecompilerId = "r2ghidra";
    }
    for (Decompiler *dec : decompilers) {
        ui->decompilerComboBox->addItem(dec->getName(), dec->getId());
        if (dec->getId() == selectedDecompilerId) {
            ui->decompilerComboBox->setCurrentIndex(ui->decompilerComboBox->count() - 1);
        }
    }
    decompilerSelectionEnabled = decompilers.size() > 1;
    ui->decompilerComboBox->setEnabled(decompilerSelectionEnabled);
    if (decompilers.isEmpty()) {
        ui->textEdit->setPlainText(tr("No Decompiler available."));
    }

    connect(
        ui->decompilerComboBox,
        static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this,
        &DecompilerWidget::decompilerSelected);
    connect(
        ui->optionsComboBox,
        static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
        this,
        &DecompilerWidget::optionActivated);
    refreshOptionsCombo();
    connectCursorPositionChanged(true);
    connect(seekable, &IaitoSeekable::seekableSeekChanged, this, &DecompilerWidget::seekChanged);
    ui->textEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(
        ui->textEdit,
        &QWidget::customContextMenuRequested,
        this,
        &DecompilerWidget::showDecompilerContextMenu);

    connect(Core(), &IaitoCore::breakpointsChanged, this, &DecompilerWidget::updateBreakpoints);
    mCtxMenu->addSeparator();
    mCtxMenu->addAction(&syncAction);
    addActions(mCtxMenu->actions());

    ui->progressLabel->setVisible(false);
    ui->decompileButton->setVisible(false);
    connect(
        ui->decompileButton,
        &QPushButton::clicked,
        this,
        &DecompilerWidget::startApprovedDecompilation);
    connect(ui->sourceEditFileButton, &QPushButton::clicked, this, &DecompilerWidget::editSourceFile);
    connect(
        ui->sourceOpenDirButton,
        &QPushButton::clicked,
        this,
        &DecompilerWidget::openSourceDirectory);
    connect(
        ui->sourceEditMapButton, &QPushButton::clicked, this, &DecompilerWidget::editSourceLineMap);
    connect(
        ui->sourceDirSourceButton, &QPushButton::clicked, this, &DecompilerWidget::selectSourcePath);
    updateSourceControls();
    // Setup cancel button (hidden by default)
    ui->cancelButton->setVisible(false);
    connect(ui->cancelButton, &QPushButton::clicked, this, &DecompilerWidget::cancelDecompilation);
    // Defer initial refresh until the MainWindow UI is fully ready to avoid
    // racing with other initialization commands that also touch radare2 internals.
    if (main && main->isUiReady()) {
        doRefresh();
    } else if (main) {
        connect(main, &MainWindow::uiReady, this, &DecompilerWidget::doRefresh, Qt::QueuedConnection);
    } else {
        // Fallback: no MainWindow provided, perform refresh immediately.
        doRefresh();
    }

    connect(Core(), &IaitoCore::refreshAll, this, &DecompilerWidget::doRefresh);
    connect(Core(), &IaitoCore::functionRenamed, this, &DecompilerWidget::doRefresh);
    connect(Core(), &IaitoCore::varsChanged, this, &DecompilerWidget::doRefresh);
    connect(Core(), &IaitoCore::functionsChanged, this, &DecompilerWidget::doRefresh);
    connect(Core(), &IaitoCore::flagsChanged, this, &DecompilerWidget::doRefresh);
    connect(Core(), &IaitoCore::commentsChanged, this, &DecompilerWidget::refreshIfChanged);
    connect(Core(), &IaitoCore::instructionChanged, this, &DecompilerWidget::refreshIfChanged);
    connect(Core(), &IaitoCore::refreshCodeViews, this, &DecompilerWidget::doRefresh);
    connect(Core(), &IaitoCore::refreshAll, this, &DecompilerWidget::invalidateTokenTargets);
    connect(Core(), &IaitoCore::flagsChanged, this, &DecompilerWidget::invalidateTokenTargets);
    connect(Core(), &IaitoCore::functionsChanged, this, &DecompilerWidget::invalidateTokenTargets);
    connect(Core(), &IaitoCore::functionRenamed, this, &DecompilerWidget::invalidateTokenTargets);

    // Esc to seek backward
    QAction *seekPrevAction = new QAction(this);
    ShortcutMgr()->bindAction("decompiler.seekPrev", seekPrevAction);
    seekPrevAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(seekPrevAction);
    connect(seekPrevAction, &QAction::triggered, seekable, &IaitoSeekable::seekPrev);

    QAction *switchAction = new QAction(this);
    ShortcutMgr()->bindAction("decompiler.switchToDisassembly", switchAction);
    switchAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(switchAction);
    connect(switchAction, &QAction::triggered, this, [this] {
        mainWindow->showMemoryWidget(MemoryWidgetType::Disassembly);
    });
}

DecompilerWidget::~DecompilerWidget() = default;

QWidget *DecompilerWidget::widgetToFocusOnRaise()
{
    return ui->textEdit;
}

QString DecompilerWidget::getWidgetType()
{
    return "DecompilerWidget";
}

Decompiler *DecompilerWidget::getCurrentDecompiler()
{
    return Core()->getDecompilerById(ui->decompilerComboBox->currentData().toString());
}

QString DecompilerWidget::deepLinkAt(const QPoint &pos)
{
    const QTextCursor cursor = cursorOnCharacter(pos);
    return DeepLink::linkAt(cursor.block().text(), cursor.positionInBlock());
}

QTextCursor DecompilerWidget::cursorOnCharacter(const QPoint &pos) const
{
    if (!ui->textEdit->viewport()->rect().contains(pos)) {
        return {};
    }
    QTextCursor cursor = ui->textEdit->cursorForPosition(pos);
    const QTextBlock block = cursor.block();
    const QTextLine line = block.layout()->lineForTextPosition(cursor.positionInBlock());
    if (!line.isValid()) {
        return {};
    }
    const QRect caret = ui->textEdit->cursorRect(cursor);
    const qreal x = pos.x() - caret.left() + line.cursorToX(cursor.positionInBlock());
    const QRectF bounds = line.naturalTextRect();
    if (x < bounds.left() || x >= bounds.right() || pos.y() < caret.top()
        || pos.y() >= caret.top() + caret.height()) {
        return {};
    }
    const int column = line.xToCursor(x, QTextLine::CursorOnCharacter);
    const QString text = block.text();
    if (column >= text.size() || text.at(column).isSpace()) {
        return {};
    }
    cursor.setPosition(block.position() + column);
    return cursor;
}

static QString decompilerSymbolName(QString name)
{
    // Plain-text decompilers strip r2 prefixes and turn punctuation into underscores.
    static const QRegularExpression prefix(R"(^(?:(?:sym|imp|reloc)[._])+)");
    static const QRegularExpression punctuation(R"([^\p{L}\p{N}_])");
    name = name.section(QLatin1Char('('), 0, 0).trimmed();
    name.remove(prefix);
    name.replace(punctuation, QStringLiteral("_"));
    while (name.startsWith(QLatin1Char('_'))) {
        name.remove(0, 1);
    }
    return name;
}

void DecompilerWidget::invalidateTokenTargets()
{
    symbolTargets.clear();
    outgoingXRefsCache.clear();
    symbolTargetsReady = false;
    refreshDecompilerHover();
}

void DecompilerWidget::cacheSymbolTarget(const QString &name, RVA address)
{
    const QString key = decompilerSymbolName(name);
    if (key.isEmpty()) {
        return;
    }
    auto it = symbolTargets.find(key);
    if (it == symbolTargets.end()) {
        symbolTargets.insert(key, address);
    } else if (it.value() != address) {
        // A shortened name must not silently navigate to the wrong overload/import.
        it.value() = RVA_INVALID;
    }
}

RVA DecompilerWidget::symbolTarget(const QString &name, RVA offset)
{
    if (!symbolTargetsReady) {
        auto core = Core()->core();
        r_flag_foreach(
            core->flags,
            [](RFlagItem *flag, void *user) {
                auto *widget = static_cast<DecompilerWidget *>(user);
                widget->cacheSymbolTarget(QString::fromUtf8(flag->name), flag->addr);
                widget->cacheSymbolTarget(QString::fromUtf8(flag->realname), flag->addr);
                return true;
            },
            this);
        RListIter *iter;
        RAnalFunction *function;
        IaitoRListForeach(core->anal->fcns, iter, RAnalFunction, function)
        {
            cacheSymbolTarget(QString::fromUtf8(function->name), function->addr);
        }
        symbolTargetsReady = true;
    }
    const QString key = decompilerSymbolName(name);
    const RVA target = symbolTargets.value(key, RVA_INVALID);
    if (target != RVA_INVALID || offset == RVA_INVALID) {
        return target;
    }
    auto refs = outgoingXRefsCache.constFind(offset);
    if (refs == outgoingXRefsCache.cend()) {
        refs = outgoingXRefsCache.insert(offset, Core()->getXRefs(offset, false, false));
    }
    RVA referenceTarget = RVA_INVALID;
    for (const XrefDescription &ref : refs.value()) {
        if (decompilerSymbolName(ref.to_str) == key) {
            if (referenceTarget != RVA_INVALID && referenceTarget != ref.to) {
                return RVA_INVALID;
            }
            referenceTarget = ref.to;
        }
    }
    return referenceTarget;
}

RVA DecompilerWidget::tokenTarget(QTextCursor &cursor)
{
    if (cursor.isNull()) {
        return RVA_INVALID;
    }
    const size_t pos = static_cast<size_t>(cursor.position());
    RVA offset = RVA_INVALID;
    size_t offsetStart = 0;
#if R2_ABIVERSION >= 40
    RCodeMetaItem *annotation;
    R_VEC_FOREACH(&code->annotations, annotation)
    {
#else
    void *iter;
    r_vector_foreach(&code->annotations, iter)
    {
        RCodeMetaItem *annotation = reinterpret_cast<RCodeMetaItem *>(iter);
#endif
        if (annotation->start > pos || annotation->end <= pos) {
            continue;
        }
        if (annotation->type == R_CODEMETA_TYPE_OFFSET && annotation->start >= offsetStart) {
            offsetStart = annotation->start;
            offset = annotation->offset.offset;
        }
        if (r_codemeta_item_is_reference(annotation)) {
            cursor.setPosition(static_cast<int>(annotation->start));
            cursor.setPosition(static_cast<int>(annotation->end), QTextCursor::KeepAnchor);
            return annotation->reference.offset;
        }
        if (r_codemeta_item_is_variable(annotation)) {
            return RVA_INVALID;
        }
    }

    // Decompilers without reference annotations still expose addresses and symbols.
    static const QRegularExpression tokens(
        R"(\b(?:0[xX][0-9a-fA-F]+|[\p{L}_$][\p{L}\p{N}_.$]*(?:::[\p{L}_$~][\p{L}\p{N}_.$]*)*)\b)",
        QRegularExpression::UseUnicodePropertiesOption);
    auto matches = tokens.globalMatch(cursor.block().text());
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (cursor.positionInBlock() < match.capturedStart()
            || cursor.positionInBlock() >= match.capturedEnd()) {
            continue;
        }
        const QString token = match.captured();
        const int start = cursor.block().position() + match.capturedStart();
        cursor.setPosition(start);
        cursor.setPosition(start + match.capturedLength(), QTextCursor::KeepAnchor);
        if (token.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
            bool ok = false;
            const RVA address = token.toULongLong(&ok, 16);
            return ok ? address : RVA_INVALID;
        }
        {
            RCoreLocked core = Core()->core();
            const QByteArray name = token.toUtf8();
            if (RFlagItem *flag = r_flag_get(core->flags, name.constData())) {
                return flag->addr;
            }
            if (RAnalFunction *function = r_anal_get_function_byname(core->anal, name.constData())) {
                return function->addr;
            }
        }
        return symbolTarget(token, offset);
    }
    return RVA_INVALID;
}

ut64 DecompilerWidget::offsetForPosition(size_t pos)
{
    ut64 closestPos = UT64_MAX;
    ut64 closestOffset = mCtxMenu->getFirstOffsetInLine();
#if R2_ABIVERSION >= 40
    RCodeMetaItem *annotation;
    R_VEC_FOREACH(&code->annotations, annotation)
    {
#else
    void *iter;
    r_vector_foreach(&code->annotations, iter)
    {
        RCodeMetaItem *annotation = reinterpret_cast<RCodeMetaItem *>(iter);
#endif
        if (annotation->type != R_CODEMETA_TYPE_OFFSET || annotation->start > pos
            || annotation->end <= pos) {
            continue;
        }
        if (closestPos != UT64_MAX && closestPos >= annotation->start) {
            continue;
        }
        closestPos = annotation->start;
        closestOffset = annotation->offset.offset;
    }
    // eprintf ("%llx \n", closestOffset);
    if (closestOffset != UT64_MAX) {
        this->currentOffset = closestOffset;
    }
    return closestOffset;
}

size_t DecompilerWidget::positionForOffset(ut64 offset)
{
    ut64 closestPos = UT64_MAX;
    ut64 closestOffset = UT64_MAX;
#if R2_ABIVERSION >= 40
    RCodeMetaItem *annotation;
    R_VEC_FOREACH(&code->annotations, annotation)
    {
#else
    void *iter;
    r_vector_foreach(&code->annotations, iter)
    {
        RCodeMetaItem *annotation = reinterpret_cast<RCodeMetaItem *>(iter);
#endif
        if (annotation->type != R_CODEMETA_TYPE_OFFSET || annotation->offset.offset > offset) {
            continue;
        }
        if (closestOffset != UT64_MAX && closestOffset >= annotation->offset.offset) {
            continue;
        }
        closestPos = annotation->start;
        closestOffset = annotation->offset.offset;
    }
    if (closestPos != UT64_MAX) {
        // Some decompilers start offset annotations on the preceding newline.
        QTextCursor cursor(ui->textEdit->document());
        cursor.setPosition(static_cast<int>(closestPos));
        if (cursor.atBlockEnd() && cursor.block().next().isValid()) {
            closestPos++;
        }
    }
    return closestPos;
}

void DecompilerWidget::updateBreakpoints(RVA addr)
{
    if (!addressInRange(addr)) {
        return;
    }
    setInfoForBreakpoints();
    QTextCursor cursor = ui->textEdit->textCursor();
    cursor.select(QTextCursor::Document);
    cursor.setCharFormat(QTextCharFormat());
    cursor.setBlockFormat(QTextBlockFormat());
    ui->textEdit->setExtraSelections({});
    highlightPC();
    highlightBreakpoints();
    updateSelection();
}

void DecompilerWidget::setInfoForBreakpoints()
{
    if (mCtxMenu->getIsTogglingBreakpoints()) {
        return;
    }
    // Get the range of the line
    QTextCursor cursorForLine = ui->textEdit->textCursor();
    cursorForLine.movePosition(QTextCursor::StartOfLine);
    size_t startPos = cursorForLine.position();
    cursorForLine.movePosition(QTextCursor::EndOfLine);
    size_t endPos = cursorForLine.position();
    gatherBreakpointInfo(*code, startPos, endPos);
}

void DecompilerWidget::gatherBreakpointInfo(RCodeMeta &codeDecompiled, size_t startPos, size_t endPos)
{
    RVA firstOffset = RVA_MAX;
#if R2_ABIVERSION >= 40
    RCodeMetaItem *annotation;
    R_VEC_FOREACH(&codeDecompiled.annotations, annotation)
    {
#else
    void *iter;
    r_vector_foreach(&codeDecompiled.annotations, iter)
    {
        RCodeMetaItem *annotation = reinterpret_cast<RCodeMetaItem *>(iter);
#endif
        if (annotation->type != R_CODEMETA_TYPE_OFFSET) {
            continue;
        }
        if (annotation->start < endPos && annotation->end > startPos
            && annotation->offset.offset != RVA_INVALID) {
            firstOffset = (annotation->offset.offset < firstOffset) ? annotation->offset.offset
                                                                    : firstOffset;
        }
    }
    mCtxMenu->setFirstOffsetInLine(firstOffset);
    QList<RVA> functionBreakpoints = Core()->getBreakpointsInFunction(decompiledFunctionAddr);
    QVector<RVA> offsetList;
    for (RVA bpOffset : functionBreakpoints) {
        size_t pos = positionForOffset(bpOffset);
        if (startPos <= pos && pos <= endPos) {
            offsetList.push_back(bpOffset);
        }
    }
    std::sort(offsetList.begin(), offsetList.end());
    mCtxMenu->setAvailableBreakpoints(offsetList);
}

void DecompilerWidget::refreshIfChanged(RVA addr)
{
    if (addressInRange(addr)
        || (decompiledFunctionAddr != RVA_INVALID
            && Core()->getFunctionStart(addr) == decompiledFunctionAddr)) {
        doRefresh();
    }
}

void DecompilerWidget::doRefresh()
{
    if (auto mw = qobject_cast<MainWindow *>(this->parentWidget())) {
        if (!mw->isUiReady()) {
            return;
        }
    }

    RVA addr = seekable->getOffset();
    if (!refreshDeferrer->attemptRefresh(nullptr)) {
        return;
    }
    if (ui->decompilerComboBox->currentIndex() < 0) {
        return;
    }
    Decompiler *dec = getCurrentDecompiler();
    if (!dec) {
        return;
    }

    ui->progressLabel->setText(tr("Decompiling..."));
    ui->progressLabel->setVisible(false);
    ui->decompileButton->setVisible(false);
    ui->cancelButton->setVisible(false);

    if (currentDecompileTask && currentDecompileTask->isRunning()) {
        currentDecompileTask->interrupt();
    }

    ui->textEdit->setExtraSelections({});
    previousFunctionAddr = decompiledFunctionAddr;
    decompiledFunctionAddr = Core()->getFunctionStart(addr);
    updateWindowTitle();
    if (decompiledFunctionAddr == RVA_INVALID) {
        approvedDecompilationFunctionAddr = RVA_INVALID;
        setCode(
            Decompiler::makeWarning(
                tr("No function found at this offset. "
                   "Seek to a function or define one in order to decompile it.")));
        updateSourceControls();
        return;
    }

    mCtxMenu->setDecompiledFunctionAddress(decompiledFunctionAddr);

    if (dec->requiresManualTrigger() && !dec->hasCachedResult(decompiledFunctionAddr)
        && approvedDecompilationFunctionAddr != decompiledFunctionAddr) {
        ui->progressLabel->setText(tr("r2ai decompilation is not cached"));
        ui->progressLabel->setVisible(true);
        ui->decompileButton->setVisible(true);
        setCode(
            Decompiler::makeWarning(
                tr("r2ai decompilation can contact the configured LLM.\n"
                   "Press Decompile to run it for this function.")));
        lowestOffsetInCode = RVA_MAX;
        highestOffsetInCode = 0;
        updateSourceControls();
        return;
    }

    if (approvedDecompilationFunctionAddr == decompiledFunctionAddr) {
        approvedDecompilationFunctionAddr = RVA_INVALID;
    }

    ui->progressLabel->setText(
        dec->requiresManualTrigger() ? tr("r2ai is processing...") : tr("Decompiling..."));
    ui->progressLabel->setVisible(true);
    ui->cancelButton->setVisible(true);
    if (dec->requiresManualTrigger()) {
        setCode(
            Decompiler::makeWarning(
                tr("r2ai is processing this function.\n"
                   "The result will be cached when it completes.")));
    }

    DecompileTask *task = new DecompileTask(dec, addr);
    AsyncTask::Ptr taskPtr(task);
    currentDecompileTask = taskPtr;
    decompilerBusy = true;

    connect(
        task,
        &AsyncTask::finished,
        this,
        [this, task]() {
            ui->progressLabel->setVisible(false);
            ui->decompileButton->setVisible(false);
            ui->cancelButton->setVisible(false);

            RCodeMeta *cm = task->getCode();
            currentDecompileTask.clear();
            decompilerBusy = false;
            if (cm) {
                this->decompilationFinished(cm);
            } else {
                this->decompilationFinished(
                    Decompiler::makeWarning(
                        tr("Cannot decompile at this address (Not a function?)")));
            }
        },
        Qt::QueuedConnection);

    Core()->getAsyncTaskManager()->start(taskPtr);
}

void DecompilerWidget::refreshDecompiler()
{
    doRefresh();
    setInfoForBreakpoints();
}

QTextCursor DecompilerWidget::getCursorForAddress(RVA addr)
{
    size_t pos = positionForOffset(addr);
    if (pos == UT64_MAX) {
        return QTextCursor();
    }
    QTextCursor cursor = ui->textEdit->textCursor();
    cursor.setPosition(pos);
    return cursor;
}

void DecompilerWidget::decompilationFinished(RCodeMeta *codeDecompiled)
{
    bool isDisplayReset = false;
    int cursorLineOffset = 0;
    int cursorColumn = 0;
    bool restoreCursor = false;
    if (previousFunctionAddr == decompiledFunctionAddr) {
        scrollerHorizontal = ui->textEdit->horizontalScrollBar()->sliderPosition();
        scrollerVertical = ui->textEdit->verticalScrollBar()->sliderPosition();
        isDisplayReset = true;
        const QTextCursor cursor = ui->textEdit->textCursor();
        const QTextCursor addressCursor = getCursorForAddress(seekable->getOffset());
        if (!addressCursor.isNull()) {
            cursorLineOffset = cursor.blockNumber() - addressCursor.blockNumber();
            cursorColumn = cursor.positionInBlock();
            restoreCursor = true;
        }
    }

    ui->progressLabel->setVisible(false);
    ui->decompileButton->setVisible(false);
    // per user request: never disable the decompiler selection combobox

    mCtxMenu->setAnnotationHere(nullptr);
    setCode(codeDecompiled);
    updateSourceControls();

    Decompiler *dec = getCurrentDecompiler();
    QObject::disconnect(dec, &Decompiler::finished, this, &DecompilerWidget::decompilationFinished);
    decompilerBusy = false;

    if (ui->textEdit->toPlainText().isEmpty()) {
        setCode(Decompiler::makeWarning(tr("Cannot decompile at this address (Not a function?)")));
        lowestOffsetInCode = RVA_MAX;
        highestOffsetInCode = 0;
        return;
    }
    updateCursorPosition();
    if (restoreCursor) {
        const QTextBlock block = ui->textEdit->document()->findBlockByNumber(
            ui->textEdit->textCursor().blockNumber() + cursorLineOffset);
        if (block.isValid()) {
            connectCursorPositionChanged(false);
            QTextCursor cursor(block);
            cursor.setPosition(block.position() + qMin(cursorColumn, block.length() - 1));
            ui->textEdit->setTextCursor(cursor);
            updateCursorContext();
            updateSelection();
            connectCursorPositionChanged(true);
        }
    }
    highlightPC();
    highlightBreakpoints();
    lowestOffsetInCode = RVA_MAX;
    highestOffsetInCode = 0;
#if R2_ABIVERSION >= 40
    RCodeMetaItem *annotation;
    R_VEC_FOREACH(&code->annotations, annotation)
    {
#else
    void *iter;
    r_vector_foreach(&code->annotations, iter)
    {
        RCodeMetaItem *annotation = reinterpret_cast<RCodeMetaItem *>(iter);
#endif
        if (annotation->type == R_CODEMETA_TYPE_OFFSET) {
            if (lowestOffsetInCode > annotation->offset.offset) {
                lowestOffsetInCode = annotation->offset.offset;
            }
            if (highestOffsetInCode < annotation->offset.offset) {
                highestOffsetInCode = annotation->offset.offset;
            }
        }
    }

    if (isDisplayReset) {
        ui->textEdit->horizontalScrollBar()->setSliderPosition(scrollerHorizontal);
        ui->textEdit->verticalScrollBar()->setSliderPosition(scrollerVertical);
    }
}

void DecompilerWidget::setAnnotationsAtCursor(size_t pos)
{
    RCodeMetaItem *annotationAtPos = nullptr;
#if R2_ABIVERSION >= 40
    RCodeMetaItem *annotation;
    R_VEC_FOREACH(&this->code->annotations, annotation)
    {
#else
    void *iter;
    r_vector_foreach(&this->code->annotations, iter)
    {
        RCodeMetaItem *annotation = reinterpret_cast<RCodeMetaItem *>(iter);
#endif
        if (annotation->type == R_CODEMETA_TYPE_OFFSET
            || annotation->type == R_CODEMETA_TYPE_SYNTAX_HIGHLIGHT || annotation->start > pos
            || annotation->end <= pos) {
            continue;
        }
        annotationAtPos = annotation;
        break;
    }
    mCtxMenu->setAnnotationHere(annotationAtPos);
}

void DecompilerWidget::decompilerSelected()
{
    Config()->setSelectedDecompiler(ui->decompilerComboBox->currentData().toString());
    refreshOptionsCombo();
    updateSourceControls();
    doRefresh();
}

void DecompilerWidget::refreshOptionsCombo()
{
    ui->optionsComboBox->blockSignals(true);
    ui->optionsComboBox->clear();
    Decompiler *dec = getCurrentDecompiler();
    QString prefix = dec ? dec->getConfigPrefix() : QString();
    if (prefix.isEmpty()) {
        ui->optionsComboBox->setVisible(false);
        ui->optionsComboBox->blockSignals(false);
        return;
    }
    ui->optionsComboBox->setVisible(true);
    const QStringList entries = dec->listOptions();
    for (const QString &entry : entries) {
        int eq = entry.indexOf(QLatin1Char('='));
        QString key = (eq >= 0) ? entry.left(eq).trimmed() : entry.trimmed();
        if (key.isEmpty()) {
            continue;
        }
        ui->optionsComboBox->addItem(QStringLiteral("%1.%2").arg(prefix, entry), key);
    }
    if (ui->optionsComboBox->count() == 0) {
        ui->optionsComboBox->addItem(tr("(no options)"), QString());
        ui->optionsComboBox->setEnabled(false);
    } else {
        ui->optionsComboBox->setEnabled(true);
    }
    ui->optionsComboBox->blockSignals(false);
}

void DecompilerWidget::optionActivated(int index)
{
    if (index < 0) {
        return;
    }
    Decompiler *dec = getCurrentDecompiler();
    if (!dec) {
        return;
    }
    QString key = ui->optionsComboBox->itemData(index).toString();
    if (key.isEmpty()) {
        return;
    }
    QString current = dec->getOption(key);
    QString label = QStringLiteral("%1.%2").arg(dec->getConfigPrefix(), key);
    bool ok = false;
    QString value = QInputDialog::getText(
        this,
        tr("Set %1").arg(label),
        tr("New value for %1:").arg(label),
        QLineEdit::Normal,
        current,
        &ok);
    if (!ok) {
        return;
    }
    dec->setOption(key, value);
    refreshOptionsCombo();
    doRefresh();
}

bool DecompilerWidget::isSourceDecompilerSelected() const
{
    return ui->decompilerComboBox->currentData().toString() == QLatin1String("source");
}

static RVA jsonAddress(const QJsonObject &obj)
{
    bool ok = false;
    RVA address = obj.value(QStringLiteral("addr")).toVariant().toULongLong(&ok);
    if (ok) {
        return address;
    }
    return Core()->math(obj.value(QStringLiteral("addr")).toString());
}

QVector<DecompilerWidget::SourceLocation> DecompilerWidget::sourceLocationsForCurrentFunction() const
{
    QVector<SourceLocation> result;
    if (decompiledFunctionAddr == RVA_INVALID) {
        return result;
    }

    const QJsonArray entries = Core()->cmdj(QStringLiteral("CLj")).array();
    QSet<RVA> seen;
    for (const QJsonValue &value : entries) {
        const QJsonObject obj = value.toObject();
        SourceLocation loc;
        loc.address = jsonAddress(obj);
        loc.file = obj.value(QStringLiteral("file")).toString();
        loc.line = obj.value(QStringLiteral("line")).toInt();
        loc.column = obj.contains(QStringLiteral("column"))
                         ? obj.value(QStringLiteral("column")).toInt()
                         : obj.value(QStringLiteral("colu")).toInt();
        if (!loc.isValid() || seen.contains(loc.address)) {
            continue;
        }
        if (Core()->getFunctionStart(loc.address) != decompiledFunctionAddr) {
            continue;
        }
        seen.insert(loc.address);
        result.append(loc);
    }
    std::sort(result.begin(), result.end(), [](const SourceLocation &a, const SourceLocation &b) {
        return a.address < b.address;
    });
    return result;
}

DecompilerWidget::SourceLocation DecompilerWidget::sourceLocationForAddress(RVA addr) const
{
    const QVector<SourceLocation> locations = sourceLocationsForCurrentFunction();
    SourceLocation best;
    for (const SourceLocation &loc : locations) {
        if (loc.address == addr) {
            return loc;
        }
        if (loc.address <= addr && (!best.isValid() || best.address < loc.address)) {
            best = loc;
        }
    }
    return best.isValid() || locations.isEmpty() ? best : locations.first();
}

DecompilerWidget::SourceLocation DecompilerWidget::currentSourceLocation() const
{
    RVA addr = currentOffset != UT64_MAX ? currentOffset : seekable->getOffset();
    if (addr == RVA_INVALID) {
        addr = seekable->getOffset();
    }
    return sourceLocationForAddress(addr);
}

QString DecompilerWidget::resolveSourcePath(const QString &file) const
{
    QString cleaned = QDir::cleanPath(file);
    QFileInfo info(cleaned);
    if (info.exists()) {
        return info.absoluteFilePath();
    }

    int doubleSlash = cleaned.indexOf(QStringLiteral("//"));
    while (doubleSlash >= 0) {
        QString candidate = cleaned.mid(doubleSlash + 1);
        QFileInfo candidateInfo(candidate);
        if (candidateInfo.exists()) {
            return candidateInfo.absoluteFilePath();
        }
        doubleSlash = cleaned.indexOf(QStringLiteral("//"), doubleSlash + 2);
    }

    const QString sourceDir = Core()->getConfig(QStringLiteral("dir.source"));
    if (!sourceDir.isEmpty()) {
        const QFileInfo joinedInfo(QDir(sourceDir).filePath(cleaned));
        if (joinedInfo.exists()) {
            return joinedInfo.absoluteFilePath();
        }
        const QFileInfo basenameInfo(QDir(sourceDir).filePath(QFileInfo(cleaned).fileName()));
        if (basenameInfo.exists()) {
            return basenameInfo.absoluteFilePath();
        }
    }

    return cleaned;
}

void DecompilerWidget::updateSourceControls()
{
    const bool sourceSelected = isSourceDecompilerSelected();
    ui->sourceEditFileButton->setVisible(sourceSelected);
    ui->sourceOpenDirButton->setVisible(sourceSelected);
    ui->sourceEditMapButton->setVisible(sourceSelected);
    ui->sourceDirSourceButton->setVisible(sourceSelected);

    if (!sourceSelected) {
        return;
    }

    const SourceLocation loc = currentSourceLocation();
    const QString path = loc.isValid() ? resolveSourcePath(loc.file) : QString();
    const QFileInfo fileInfo(path);
    const bool fileExists = loc.isValid() && fileInfo.exists() && fileInfo.isFile();
    ui->sourceEditFileButton->setEnabled(fileExists);
    ui->sourceOpenDirButton->setEnabled(loc.isValid() && fileInfo.absoluteDir().exists());
    ui->sourceEditMapButton->setEnabled(decompiledFunctionAddr != RVA_INVALID);
    ui->sourceDirSourceButton->setEnabled(true);
    if (loc.isValid()) {
        ui->sourceEditFileButton->setToolTip(path);
        ui->sourceOpenDirButton->setToolTip(fileInfo.absolutePath());
    } else {
        ui->sourceEditFileButton->setToolTip(tr("No source file is mapped at the current address."));
        ui->sourceOpenDirButton->setToolTip(
            tr("No source directory is mapped at the current address."));
    }
}

void DecompilerWidget::editSourceFile()
{
    const SourceLocation loc = currentSourceLocation();
    if (!loc.isValid()) {
        return;
    }
    const QString path = resolveSourcePath(loc.file);
    if (!QFileInfo(path).isFile()) {
        Core()->message(tr("Source file not found: %1").arg(path));
        return;
    }
    if (!openTextEditDialogFromFile(path, this)) {
        Core()->message(tr("Failed to save source file: %1").arg(path));
    }
}

void DecompilerWidget::openSourceDirectory()
{
    const SourceLocation loc = currentSourceLocation();
    if (!loc.isValid()) {
        return;
    }
    const QFileInfo fileInfo(resolveSourcePath(loc.file));
    if (!fileInfo.absoluteDir().exists()) {
        Core()->message(tr("Source directory not found: %1").arg(fileInfo.absolutePath()));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
}

QString DecompilerWidget::sourceMapText(const QVector<SourceLocation> &locations) const
{
    QStringList rows;
    for (const SourceLocation &loc : locations) {
        rows.append(QStringLiteral("0x%1 %2:%3")
                        .arg(static_cast<qulonglong>(loc.address), 0, 16)
                        .arg(loc.file)
                        .arg(loc.line));
    }
    return rows.join(QLatin1Char('\n'));
}

bool DecompilerWidget::applySourceMapText(
    const QString &text, const QVector<SourceLocation> &oldLocations)
{
    struct Mapping
    {
        RVA address = RVA_INVALID;
        QString fileLine;
    };

    QVector<Mapping> mappings;
    int lineNumber = 0;
    for (const QString &rawLine : text.split(QLatin1Char('\n'))) {
        lineNumber++;
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        int split = -1;
        for (int i = 0; i < line.size(); i++) {
            if (line.at(i).isSpace()) {
                split = i;
                break;
            }
        }
        if (split < 0) {
            Core()->message(tr("Invalid source map row %1").arg(lineNumber));
            return false;
        }

        bool ok = false;
        const RVA address = line.left(split).toULongLong(&ok, 0);
        const QString fileLine = line.mid(split + 1).trimmed();
        const int colon = fileLine.lastIndexOf(QLatin1Char(':'));
        if (!ok || colon <= 0 || colon == fileLine.size() - 1) {
            Core()->message(tr("Invalid source map row %1").arg(lineNumber));
            return false;
        }
        bool lineOk = false;
        fileLine.mid(colon + 1).toInt(&lineOk);
        if (!lineOk) {
            Core()->message(tr("Invalid source line number on row %1").arg(lineNumber));
            return false;
        }
        mappings.append({address, fileLine});
    }

    QSet<RVA> resetAddresses;
    for (const SourceLocation &loc : oldLocations) {
        if (loc.isValid() && !resetAddresses.contains(loc.address)) {
            Core()->cmdRawAt(QStringLiteral("CL-"), loc.address);
            resetAddresses.insert(loc.address);
        }
    }
    for (const Mapping &mapping : mappings) {
        Core()->cmdRawAt(
            QStringLiteral("CL+%1").arg(Core()->sanitizeStringForCommand(mapping.fileLine)),
            mapping.address);
    }
    return true;
}

void DecompilerWidget::editSourceLineMap()
{
    const QVector<SourceLocation> locations = sourceLocationsForCurrentFunction();
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this,
        tr("Edit Source Line Map"),
        tr("Use one row per mapping: address file:line"),
        sourceMapText(locations),
        &ok);
    if (!ok) {
        return;
    }
    if (applySourceMapText(text, locations)) {
        doRefresh();
    }
}

void DecompilerWidget::selectSourcePath()
{
    QString current = Core()->getConfig(QStringLiteral("dir.source"));
    if (current.isEmpty()) {
        const SourceLocation loc = currentSourceLocation();
        if (loc.isValid()) {
            current = QFileInfo(resolveSourcePath(loc.file)).absolutePath();
        }
    }

    QFileDialog dialog(this, tr("Select Source File or Directory"), current);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setOption(QFileDialog::ReadOnly, true);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return;
    }

    QFileInfo selected(dialog.selectedFiles().first());
    const QString sourceDir = selected.isDir() ? selected.absoluteFilePath()
                                               : selected.absolutePath();
    if (sourceDir.isEmpty()) {
        return;
    }
    Config()->setConfig(QStringLiteral("dir.source"), QDir::cleanPath(sourceDir));
    doRefresh();
}

void DecompilerWidget::cancelDecompilation()
{
    if (currentDecompileTask && currentDecompileTask->isRunning()) {
        currentDecompileTask->interrupt();
        ui->decompileButton->setVisible(false);
        ui->cancelButton->setVisible(false);
        ui->progressLabel->setText(tr("Cancelling..."));
    }
}

void DecompilerWidget::startApprovedDecompilation()
{
    RVA functionAddr = Core()->getFunctionStart(seekable->getOffset());
    if (functionAddr == RVA_INVALID) {
        functionAddr = seekable->getOffset();
    }
    approvedDecompilationFunctionAddr = functionAddr;
    doRefresh();
}

void DecompilerWidget::connectCursorPositionChanged(bool connectPositionChange)
{
    if (!connectPositionChange) {
        disconnect(
            ui->textEdit,
            &QPlainTextEdit::cursorPositionChanged,
            this,
            &DecompilerWidget::cursorPositionChanged);
    } else {
        connect(
            ui->textEdit,
            &QPlainTextEdit::cursorPositionChanged,
            this,
            &DecompilerWidget::cursorPositionChanged);
    }
}

void DecompilerWidget::cursorPositionChanged()
{
    const RVA offset = updateCursorContext();
    // Do not perform seeks along with the cursor while selecting multiple lines
    if (!ui->textEdit->textCursor().selectedText().isEmpty()) {
        return;
    }

    if (offset != RVA_INVALID && offset != seekable->getOffset()) {
        seekFromCursor = true;
        seekable->seek(offset);
        seekFromCursor = false;
    }
    updateSourceControls();
    updateSelection();
}

RVA DecompilerWidget::updateCursorContext()
{
    const size_t pos = ui->textEdit->textCursor().position();
    setAnnotationsAtCursor(pos);
    setInfoForBreakpoints();
    const RVA offset = offsetForPosition(pos);
    mCtxMenu->setOffset(offset);
    return offset;
}

void DecompilerWidget::seekChanged()
{
    if (seekFromCursor) {
        return;
    }
    RVA fcnAddr = Core()->getFunctionStart(seekable->getOffset());
    if (fcnAddr == RVA_INVALID || fcnAddr != decompiledFunctionAddr) {
        doRefresh();
        return;
    }
    updateCursorPosition();
}

void DecompilerWidget::updateCursorPosition()
{
    RVA offset = seekable->getOffset();
    size_t pos = positionForOffset(offset);
    if (pos == UT64_MAX) {
        return;
    }
    mCtxMenu->setOffset(offset);
    connectCursorPositionChanged(false);
    QTextCursor cursor = ui->textEdit->textCursor();
    cursor.setPosition(pos);
    ui->textEdit->setTextCursor(cursor);
    updateCursorContext();
    updateSelection();
    connectCursorPositionChanged(true);
}

void DecompilerWidget::setupFonts()
{
    ui->textEdit->setFont(Config()->getFont());
}

void DecompilerWidget::updateSelection()
{
    QList<QTextEdit::ExtraSelection> extraSelections;

    // Highlight the current line
    QTextCursor cursor = ui->textEdit->textCursor();
    extraSelections.append(createLineHighlightSelection(cursor));

    // Highlight all the words in the document same as the current one
    cursor.select(QTextCursor::WordUnderCursor);
    QString searchString = cursor.selectedText();
    mCtxMenu->setCurHighlightedWord(searchString);
    extraSelections.append(createSameWordsSelections(ui->textEdit, searchString));

    // Highlight matches from scr.highlight
    QString scrHighlight = Core()->getConfig("scr.highlight");
    extraSelections.append(createHighlightSelections(ui->textEdit, scrHighlight));

    setDecompilerSelections(extraSelections);
    // Highlight PC after updating the selected line
    highlightPC();
}

QString DecompilerWidget::getWindowTitle() const
{
    return tr("Decompiler");
}

void DecompilerWidget::fontsUpdatedSlot()
{
    setupFonts();
    refreshDecompilerHover();
}

void DecompilerWidget::colorsUpdatedSlot()
{
    // When only the interface palette changed, skip the syntax re-highlight pass.
    if (Config()->isInterfacePaletteOnlyUpdate()) {
        return;
    }
    if (syntaxHighlighter) {
        QMetaObject::invokeMethod(syntaxHighlighter, "rehighlight", Qt::QueuedConnection);
    }
    setDecompilerSelections(ui->textEdit->extraSelections());
}

void DecompilerWidget::showDecompilerContextMenu(const QPoint &pt)
{
    updateCursorContext();
    mCtxMenu->exec(ui->textEdit->viewport()->mapToGlobal(pt));
}

void DecompilerWidget::seekToReference()
{
    QTextCursor cursor = ui->textEdit->textCursor();
    const RVA target = tokenTarget(cursor);
    if (target != RVA_INVALID) {
        seekable->seek(target);
        return;
    }
    size_t pos = ui->textEdit->textCursor().position();
    RVA offset = offsetForPosition(pos);
    seekable->seekToReference(offset);
}

void DecompilerWidget::setDecompilerSelections(QList<QTextEdit::ExtraSelection> selections)
{
    constexpr int hoverProperty = QTextFormat::UserProperty;
    selections.erase(
        std::remove_if(
            selections.begin(),
            selections.end(),
            [](const QTextEdit::ExtraSelection &s) { return s.format.boolProperty(hoverProperty); }),
        selections.end());
    if (hoveredToken.hasSelection()) {
        QTextEdit::ExtraSelection hover;
        hover.cursor = hoveredToken;
        hover.format.setBackground(ConfigColor("wordHighlight"));
        hover.format.setProperty(hoverProperty, true);
        selections.append(hover);
    }
    ui->textEdit->setExtraSelections(selections);
}

void DecompilerWidget::setHoveredToken(const QTextCursor &cursor)
{
    if (hoveredToken == cursor) {
        return;
    }
    hoveredToken = cursor;
    setDecompilerSelections(ui->textEdit->extraSelections());
}

void DecompilerWidget::updateDecompilerHover(const QPoint &pos, Qt::MouseButtons buttons)
{
    QTextCursor hover;
    Qt::CursorShape shape = Qt::ArrowCursor;
    if (buttons & Qt::LeftButton) {
        shape = Qt::IBeamCursor;
    } else {
        QTextCursor cursor = cursorOnCharacter(pos);
        const QString text = cursor.block().text();
        const QString link = DeepLink::linkAt(text, cursor.positionInBlock());
        if (!link.isEmpty()) {
            const int start = cursor.block().position()
                              + text.lastIndexOf(link, cursor.positionInBlock());
            cursor.setPosition(start);
            cursor.setPosition(start + link.size(), QTextCursor::KeepAnchor);
            hover = cursor;
        } else if (tokenTarget(cursor) != RVA_INVALID) {
            hover = cursor;
        }
        if (hover.hasSelection()) {
            shape = Qt::PointingHandCursor;
        }
    }
    ui->textEdit->viewport()->setCursor(shape);
    setHoveredToken(hover);
}

void DecompilerWidget::refreshDecompilerHover()
{
    if (ui->textEdit->viewport()->underMouse()) {
        updateDecompilerHover(
            ui->textEdit->viewport()->mapFromGlobal(QCursor::pos()), QApplication::mouseButtons());
    } else {
        ui->textEdit->viewport()->setCursor(Qt::ArrowCursor);
        setHoveredToken({});
    }
}

bool DecompilerWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Wheel && (obj == ui->textEdit || obj == ui->textEdit->viewport())) {
        QWheelEvent *wheelEvent = static_cast<QWheelEvent *>(event);
        if (wheelEvent->modifiers() & Qt::ControlModifier) {
            int delta = wheelEvent->angleDelta().y();
            if (delta != 0) {
                qreal zoomFactor = Config()->getZoomFactor();
                zoomFactor += delta > 0 ? 0.1 : -0.1;
                Config()->setZoomFactor(zoomFactor);
            }
            event->accept();
            return true;
        }
    }
    // Handle Vim-like mark and jump in decompiler view
    if (event->type() == QEvent::KeyPress
        && (obj == ui->textEdit || obj == ui->textEdit->viewport())) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        static const struct
        {
            const char *shortcut;
            QTextCursor::MoveOperation movement;
        } movements[] = {
            {"decompiler.cursorLeft", QTextCursor::Left},
            {"decompiler.cursorDown", QTextCursor::Down},
            {"decompiler.cursorUp", QTextCursor::Up},
            {"decompiler.cursorRight", QTextCursor::Right},
        };
        for (const auto &movement : movements) {
            if (ShortcutMgr()->matches(movement.shortcut, keyEvent)) {
                ui->textEdit->moveCursor(movement.movement);
                keyEvent->accept();
                return true;
            }
        }
        if (keyEvent->modifiers() == Qt::NoModifier && keyEvent->key() == Qt::Key_M) {
            // Prepare context and compute offset for current decompiler line
            size_t pos = ui->textEdit->textCursor().position();
            setAnnotationsAtCursor(pos);
            setInfoForBreakpoints();
            ut64 offset = offsetForPosition(pos);
            if (offset == UT64_MAX) {
                offset = this->currentOffset;
            }
            mCtxMenu->setOffset(offset);
            ShortcutKeysDialog dlg(ShortcutKeysDialog::SetMark, offset, this);
            dlg.exec();
            return true;
        } else if (keyEvent->modifiers() == Qt::NoModifier && keyEvent->key() == Qt::Key_Apostrophe) {
            ShortcutKeysDialog dlg(ShortcutKeysDialog::JumpTo, RVA_INVALID, this);
            if (dlg.exec() == QDialog::Accepted) {
                QChar key = dlg.selectedKey();
                RVA addr = ShortcutKeys::instance()->getMark(key);
                seekable->seek(addr);
            }
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonDblClick
        && (obj == ui->textEdit || obj == ui->textEdit->viewport())) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        pressedTokenTarget = RVA_INVALID;
        if (mouseEvent->button() != Qt::LeftButton || followedTokenOnClick) {
            return true;
        }
        ui->textEdit->setTextCursor(ui->textEdit->cursorForPosition(mouseEvent->pos()));
        seekToReference();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress
        && (obj == ui->textEdit || obj == ui->textEdit->viewport())) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        pressedTokenTarget = RVA_INVALID;
        followedTokenOnClick = false;
        if (mouseEvent->button() == Qt::LeftButton && mouseEvent->modifiers() == Qt::NoModifier) {
            tokenPressPosition = mouseEvent->pos();
            QTextCursor cursor = cursorOnCharacter(tokenPressPosition);
            pressedTokenTarget = tokenTarget(cursor);
        }
        updateDecompilerHover(mouseEvent->pos(), mouseEvent->buttons());
        if (mouseEvent->button() == Qt::RightButton && !ui->textEdit->textCursor().hasSelection()) {
            ui->textEdit->setTextCursor(ui->textEdit->cursorForPosition(mouseEvent->pos()));
            updateCursorContext();
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonRelease
        && (obj == ui->textEdit || obj == ui->textEdit->viewport())) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        const RVA target = pressedTokenTarget;
        pressedTokenTarget = RVA_INVALID;
        updateDecompilerHover(mouseEvent->pos(), mouseEvent->buttons());
        if (mouseEvent->button() == Qt::LeftButton && !ui->textEdit->textCursor().hasSelection()) {
            const QString link = deepLinkAt(mouseEvent->pos());
            if (!link.isEmpty()) {
                DeepLink::handle(mainWindow, link);
                return true;
            }
            QTextCursor cursor = cursorOnCharacter(mouseEvent->pos());
            if (target != RVA_INVALID
                && (mouseEvent->pos() - tokenPressPosition).manhattanLength()
                       < QApplication::startDragDistance()
                && target == tokenTarget(cursor)) {
                followedTokenOnClick = true;
                seekable->seek(target);
                refreshDecompilerHover();
                return true;
            }
        }
    }
    if (event->type() == QEvent::MouseMove
        && (obj == ui->textEdit || obj == ui->textEdit->viewport())) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if ((mouseEvent->buttons() & Qt::LeftButton)
            && (mouseEvent->pos() - tokenPressPosition).manhattanLength()
                   >= QApplication::startDragDistance()) {
            pressedTokenTarget = RVA_INVALID;
        }
        updateDecompilerHover(mouseEvent->pos(), mouseEvent->buttons());
    }
    if (event->type() == QEvent::Leave && obj == ui->textEdit->viewport()) {
        ui->textEdit->viewport()->setCursor(Qt::ArrowCursor);
        setHoveredToken({});
    }
    return MemoryDockWidget::eventFilter(obj, event);
}

void DecompilerWidget::highlightPC()
{
    RVA PCAddress = Core()->getProgramCounterValue();
    if (PCAddress == RVA_INVALID
        || (Core()->getFunctionStart(PCAddress) != decompiledFunctionAddr)) {
        return;
    }

    QTextCursor cursor = getCursorForAddress(PCAddress);
    if (!cursor.isNull()) {
        colorLine(createLineHighlightPC(cursor));
    }
}

void DecompilerWidget::highlightBreakpoints()
{
    QList<RVA> functionBreakpoints = Core()->getBreakpointsInFunction(decompiledFunctionAddr);
    QTextCursor cursor;
    for (RVA &bp : functionBreakpoints) {
        if (bp == RVA_INVALID) {
            continue;
        }
        cursor = getCursorForAddress(bp);
        if (!cursor.isNull()) {
            // Use a Block formatting since these lines are not updated
            // frequently as selections and PC
            QTextBlockFormat f;
            f.setBackground(ConfigColor("gui.breakpoint_background"));
            cursor.setBlockFormat(f);
        }
    }
}

bool DecompilerWidget::colorLine(QTextEdit::ExtraSelection extraSelection)
{
    QList<QTextEdit::ExtraSelection> extraSelections = ui->textEdit->extraSelections();
    extraSelections.append(extraSelection);
    setDecompilerSelections(extraSelections);
    return true;
}

void DecompilerWidget::copy()
{
    if (ui->textEdit->textCursor().hasSelection()) {
        ui->textEdit->copy();
    } else {
        QTextCursor cursor = ui->textEdit->textCursor();
        QClipboard *clipboard = QApplication::clipboard();
        cursor.select(QTextCursor::WordUnderCursor);
        if (!cursor.selectedText().isEmpty()) {
            clipboard->setText(cursor.selectedText());
        } else {
            cursor.select(QTextCursor::LineUnderCursor);
            clipboard->setText(cursor.selectedText());
        }
    }
}

bool DecompilerWidget::addressInRange(RVA addr)
{
    if (lowestOffsetInCode <= addr && addr <= highestOffsetInCode) {
        return true;
    }
    return false;
}

/**
 * Convert annotation ranges from byte offsets in utf8 used by RAnnotated code
 * to QString QChars used by QString and Qt text editor.
 * @param code - RAnnotated code with annotations that need to be modified
 * @return Decompiled code
 */
static QString remapAnnotationOffsetsToQString(RCodeMeta &code)
{
// XXX this is slow as hell
// XXX lets just disable it for now
#if 1
    QString text(code.code);
#else
    QByteArray bytes(code.code);
    QString text;
    text.reserve(bytes.size()); // not exact but a reasonable approximation
    QTextStream stream(bytes);
    stream.setCodec("UTF-8");
    std::vector<size_t> offsets;
    offsets.reserve(bytes.size());
    offsets.push_back(0);
    QChar singleChar;
    while (!stream.atEnd()) {
        stream >> singleChar;
        if (singleChar == QChar('/')) {
        }
        text.append(singleChar);
        offsets.push_back(stream.pos());
    }
    auto mapPos = [&](size_t pos) {
        auto it = std::upper_bound(offsets.begin(), offsets.end(), pos);
        if (it != offsets.begin()) {
            --it;
        }
        return it - offsets.begin();
    };

#if R2_ABIVERSION >= 40
    RCodeMetaItem *annotation;
    R_VEC_FOREACH(&code.annotations, annotation)
    {
#else
    void *iter;
    r_vector_foreach(&code.annotations, iter)
    {
        RCodeMetaItem *annotation = reinterpret_cast<RCodeMetaItem *>(iter);
#endif
        annotation->start = mapPos(annotation->start);
        annotation->end = mapPos(annotation->end);
    }
#endif
    return text;
}

static bool hasCodeMetaSyntaxHighlights(RCodeMeta *code)
{
    if (!code) {
        return false;
    }
#if R2_ABIVERSION >= 40
    RCodeMetaItem *annotation;
    R_VEC_FOREACH(&code->annotations, annotation)
    {
#else
    void *iter;
    r_vector_foreach(&code->annotations, iter)
    {
        RCodeMetaItem *annotation = reinterpret_cast<RCodeMetaItem *>(iter);
#endif
        if (annotation->type == R_CODEMETA_TYPE_SYNTAX_HIGHLIGHT) {
            return true;
        }
    }
    return false;
}

static RCodeMeta *copyCodeMeta(RCodeMeta *source)
{
    RCodeMeta *copy = r_codemeta_new("");
    free(copy->code);
    copy->code = strdup(source && source->code ? source->code : "");
    if (!source) {
        return copy;
    }

#if R2_ABIVERSION >= 40
    RCodeMetaItem *oldItem;
    R_VEC_FOREACH(&source->annotations, oldItem)
    {
#else
    void *iter;
    r_vector_foreach(&source->annotations, iter)
    {
        RCodeMetaItem *oldItem = reinterpret_cast<RCodeMetaItem *>(iter);
#endif
        RCodeMetaItem *newItem = r_codemeta_item_new();
        r_codemeta_item_copy(newItem, oldItem);
        r_codemeta_add_item(copy, newItem);
    }
    return copy;
}

struct DecompilerColumnLine
{
    size_t start = 0;
    size_t end = 0;
    size_t nextStart = 0;
    size_t offsetAnnotationStart = SIZE_MAX;
    size_t shiftBefore = 0;
    size_t shiftInLine = 0;
    RVA address = RVA_INVALID;
    QString disassembly = {};
};

static QVector<DecompilerColumnLine> collectAddressLines(const QString &text)
{
    QVector<DecompilerColumnLine> lines;
    int start = 0;
    while (start < text.size()) {
        int newline = text.indexOf(QLatin1Char('\n'), start);
        int end = newline < 0 ? text.size() : newline;
        int nextStart = newline < 0 ? text.size() : newline + 1;
        lines.append(
            {static_cast<size_t>(start), static_cast<size_t>(end), static_cast<size_t>(nextStart)});
        start = nextStart;
    }
    return lines;
}

static bool assignLineAddresses(QVector<DecompilerColumnLine> &lines, RCodeMeta *code)
{
    bool hasAddress = false;
#if R2_ABIVERSION >= 40
    RCodeMetaItem *annotation;
    R_VEC_FOREACH(&code->annotations, annotation)
    {
#else
    void *iter;
    r_vector_foreach(&code->annotations, iter)
    {
        RCodeMetaItem *annotation = reinterpret_cast<RCodeMetaItem *>(iter);
#endif
        if (annotation->type != R_CODEMETA_TYPE_OFFSET || annotation->offset.offset == RVA_INVALID) {
            continue;
        }
        for (DecompilerColumnLine &line : lines) {
            if (annotation->start >= line.end || annotation->end <= line.start) {
                continue;
            }
            if (annotation->start < line.offsetAnnotationStart) {
                line.address = annotation->offset.offset;
                line.offsetAnnotationStart = annotation->start;
                hasAddress = true;
            }
        }
    }
    return hasAddress;
}

static size_t shiftForStartPosition(const QVector<DecompilerColumnLine> &lines, size_t pos)
{
    for (const DecompilerColumnLine &line : lines) {
        if (pos < line.nextStart) {
            return line.shiftInLine;
        }
    }
    return lines.isEmpty() ? 0 : lines.last().shiftInLine;
}

static size_t shiftForEndPosition(const QVector<DecompilerColumnLine> &lines, size_t pos)
{
    for (const DecompilerColumnLine &line : lines) {
        if (pos == line.start) {
            return line.shiftBefore;
        }
        if (pos < line.nextStart) {
            return line.shiftInLine;
        }
    }
    return lines.isEmpty() ? 0 : lines.last().shiftInLine;
}

static RCodeMeta *copyCodeMetaWithColumns(
    RCodeMeta *source,
    bool showAddresses,
    bool showDisassembly,
    QHash<RVA, QString> &disassemblyCache)
{
    RCodeMeta *display = copyCodeMeta(source);
    QString text(display->code);
    QVector<DecompilerColumnLine> lines = collectAddressLines(text);
    if (!assignLineAddresses(lines, display)) {
        return display;
    }

    int addressWidth = RAddressString(0).size();
    int disassemblyWidth = 0;
    for (DecompilerColumnLine &line : lines) {
        if (line.address != RVA_INVALID) {
            addressWidth
                = std::max(addressWidth, static_cast<int>(RAddressString(line.address).size()));
            if (showDisassembly) {
                auto instruction = disassemblyCache.constFind(line.address);
                if (instruction == disassemblyCache.constEnd()) {
                    const QString assembly
                        = Core()
                              ->cmdRawAt(
                                  "pi 1 @e:scr.color=0,scr.html=false,asm.pseudo=false,"
                                  "asm.esil=false,asm.addr=false",
                                  line.address)
                              .simplified();
                    instruction = disassemblyCache.insert(line.address, assembly);
                }
                line.disassembly = instruction.value();
                disassemblyWidth
                    = std::max(disassemblyWidth, static_cast<int>(line.disassembly.size()));
            }
        }
    }

    QString rendered;
    const int prefixWidth = (showAddresses ? addressWidth + 2 : 0)
                            + (showDisassembly ? disassemblyWidth + 3 : 0);
    rendered.reserve(text.size() + lines.size() * prefixWidth);
    size_t inserted = 0;
    for (DecompilerColumnLine &line : lines) {
        QString prefix;
        if (showAddresses) {
            prefix
                = line.address == RVA_INVALID
                      ? QString(addressWidth, QLatin1Char(' '))
                      : RAddressString(line.address).rightJustified(addressWidth, QLatin1Char(' '));
            prefix.append(QStringLiteral("  "));
        }
        if (showDisassembly) {
            prefix.append(line.disassembly.leftJustified(disassemblyWidth, QLatin1Char(' ')));
            prefix.append(QStringLiteral(" | "));
        }
        line.shiftBefore = inserted;
        line.shiftInLine = inserted + static_cast<size_t>(prefix.size());
        inserted += static_cast<size_t>(prefix.size());

        rendered.append(prefix);
        rendered.append(
            text.mid(static_cast<int>(line.start), static_cast<int>(line.end - line.start)));
        if (line.nextStart > line.end) {
            rendered.append(QLatin1Char('\n'));
        }
    }

#if R2_ABIVERSION >= 40
    RCodeMetaItem *annotation;
    R_VEC_FOREACH(&display->annotations, annotation)
    {
#else
    void *iter;
    r_vector_foreach(&display->annotations, iter)
    {
        RCodeMetaItem *annotation = reinterpret_cast<RCodeMetaItem *>(iter);
#endif
        if (annotation->type == R_CODEMETA_TYPE_OFFSET) {
            for (const DecompilerColumnLine &line : lines) {
                // pdc ranges can start on the preceding newline. Only extend
                // the range to the prefix of a line with annotated text.
                if (annotation->start >= line.end || annotation->end <= line.start) {
                    continue;
                }
                annotation->start = line.start + line.shiftBefore;
                annotation->end += shiftForEndPosition(lines, annotation->end);
                break;
            }
            continue;
        }
        annotation->start += shiftForStartPosition(lines, annotation->start);
        annotation->end += shiftForEndPosition(lines, annotation->end);
    }

    QByteArray renderedBytes = rendered.toUtf8();
    free(display->code);
    display->code = strdup(renderedBytes.constData());
    return display;
}

void DecompilerWidget::setCode(RCodeMeta *newCode)
{
    symbolTargets.clear();
    outgoingXRefsCache.clear();
    symbolTargetsReady = false;
    sourceCode.reset(newCode);
    disassemblyCache.clear();
    updateDisplayedCode();
}

void DecompilerWidget::updateDisplayedCode()
{
    hoveredToken = {};
    pressedTokenTarget = RVA_INVALID;
    connectCursorPositionChanged(false);
    mCtxMenu->setAnnotationHere(nullptr);
    if (syntaxHighlighter) {
        syntaxHighlighter->setDocument(nullptr);
    }
    this->code.reset(
        showAddressColumn || showDisassemblyColumn
            ? copyCodeMetaWithColumns(
                  sourceCode.get(), showAddressColumn, showDisassemblyColumn, disassemblyCache)
            : copyCodeMeta(sourceCode.get()));
    QString text = remapAnnotationOffsetsToQString(*this->code);
    setHighlighter(hasCodeMetaSyntaxHighlights(this->code.get()));

    // To avoid invoking the highlighter's regex compilation while the
    // QTextDocument is being cleared/updated (which can cause re-entrancy
    // issues), temporarily detach the highlighter, update the document, then
    // reattach and schedule a queued rehighlight. This minimizes the risk of
    // crashes in the regex engine during synchronous document updates.
    this->ui->textEdit->setPlainText(text);
    updateCursorContext();
    connectCursorPositionChanged(true);

    if (syntaxHighlighter) {
        syntaxHighlighter->setDocument(ui->textEdit->document());
        // Use queued connection to perform rehighlight after the current
        // event has been processed to avoid re-entrancy.
        QMetaObject::invokeMethod(syntaxHighlighter, "rehighlight", Qt::QueuedConnection);
    }
    refreshDecompilerHover();
}

void DecompilerWidget::setAddressColumnVisible(bool visible)
{
    if (showAddressColumn == visible) {
        return;
    }
    showAddressColumn = visible;
    Config()->setDecompilerShowAddresses(visible);
    refreshDisplayColumns();
}

void DecompilerWidget::setDisassemblyColumnVisible(bool visible)
{
    if (showDisassemblyColumn == visible) {
        return;
    }
    showDisassemblyColumn = visible;
    Config()->setDecompilerShowDisassembly(visible);
    refreshDisplayColumns();
}

void DecompilerWidget::refreshDisplayColumns()
{
    int horizontalPosition = ui->textEdit->horizontalScrollBar()->sliderPosition();
    int verticalPosition = ui->textEdit->verticalScrollBar()->sliderPosition();
    updateDisplayedCode();
    updateCursorPosition();
    setAnnotationsAtCursor(ui->textEdit->textCursor().position());
    setInfoForBreakpoints();
    highlightPC();
    highlightBreakpoints();
    ui->textEdit->horizontalScrollBar()->setSliderPosition(horizontalPosition);
    ui->textEdit->verticalScrollBar()->setSliderPosition(verticalPosition);
}

void DecompilerWidget::setHighlighter(bool codeMetaHighlighter)
{
    if (syntaxHighlighter) {
        syntaxHighlighter->setDocument(nullptr);
        syntaxHighlighter->deleteLater();
        syntaxHighlighter = nullptr;
    }

    if (codeMetaHighlighter) {
        auto *h = new DecompilerHighlighter(nullptr);
        static_cast<DecompilerHighlighter *>(h)->setAnnotations(code.get());
        h->setParent(this);
        syntaxHighlighter = h;
    } else {
        QSyntaxHighlighter *h = Config()->createSyntaxHighlighter(nullptr);
        if (h) {
            h->setParent(this);
            syntaxHighlighter = h;
        } else {
            syntaxHighlighter = nullptr;
        }
    }
}
