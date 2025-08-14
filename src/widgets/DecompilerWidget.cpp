#include "DecompilerWidget.h"
#include "menus/DecompilerContextMenu.h"
#include "ui_DecompilerWidget.h"

#include "common/Configuration.h"
#include "common/Decompiler.h"
#include "common/DecompilerHighlighter.h"
#include "common/Helpers.h"
#include "common/IaitoSeekable.h"
#include "common/SelectionHighlight.h"
#include "common/TempConfig.h"
#include "core/MainWindow.h"

#include "common/DecompileTask.h"
#include <QPushButton>

#include "dialogs/ShortcutKeysDialog.h"
#include <QAbstractSlider>
#include <QClipboard>
#include <QKeyEvent>
#include <QObject>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockUserData>
#include <QTextEdit>

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
    , code(
          Decompiler::makeWarning(tr("Choose an offset and refresh to get decompiled code")),
          &r_codemeta_free)
{
    ui->setupUi(this);
    setObjectName(main ? main->getUniqueObjectName(getWidgetType()) : getWidgetType());
    updateWindowTitle();

    setHighlighter(Config()->isDecompilerAnnotationHighlighterEnabled());
    // Event filter to intercept key, mouse double click, and right click in the textbox
    ui->textEdit->viewport()->installEventFilter(this);
    ui->textEdit->installEventFilter(this);

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
    // Setup cancel button (hidden by default)
    ui->cancelButton->setVisible(false);
    connect(ui->cancelButton, &QPushButton::clicked, this, &DecompilerWidget::cancelDecompilation);
    doRefresh();

    connect(Core(), &IaitoCore::refreshAll, this, &DecompilerWidget::doRefresh);
    connect(Core(), &IaitoCore::functionRenamed, this, &DecompilerWidget::doRefresh);
    connect(Core(), &IaitoCore::varsChanged, this, &DecompilerWidget::doRefresh);
    connect(Core(), &IaitoCore::functionsChanged, this, &DecompilerWidget::doRefresh);
    connect(Core(), &IaitoCore::flagsChanged, this, &DecompilerWidget::doRefresh);
    connect(Core(), &IaitoCore::commentsChanged, this, &DecompilerWidget::refreshIfChanged);
    connect(Core(), &IaitoCore::instructionChanged, this, &DecompilerWidget::refreshIfChanged);
    connect(Core(), &IaitoCore::refreshCodeViews, this, &DecompilerWidget::doRefresh);

    // Esc to seek backward
    QAction *seekPrevAction = new QAction(this);
    seekPrevAction->setShortcut(Qt::Key_Escape);
    seekPrevAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(seekPrevAction);
    connect(seekPrevAction, &QAction::triggered, seekable, &IaitoSeekable::seekPrev);
}

DecompilerWidget::~DecompilerWidget() = default;

QString DecompilerWidget::getWidgetType()
{
    return "DecompilerWidget";
}

Decompiler *DecompilerWidget::getCurrentDecompiler()
{
    return Core()->getDecompilerById(ui->decompilerComboBox->currentData().toString());
}

ut64 DecompilerWidget::offsetForPosition(size_t pos)
{
    ut64 closestPos = UT64_MAX;
    ut64 closestOffset = mCtxMenu->getFirstOffsetInLine();
    void *iter;
    r_vector_foreach(&code->annotations, iter)
    {
        RCodeMetaItem *annotation = (RCodeMetaItem *) iter;
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
    void *iter;
    r_vector_foreach(&code->annotations, iter)
    {
        RCodeMetaItem *annotation = (RCodeMetaItem *) iter;
        if (annotation->type != R_CODEMETA_TYPE_OFFSET || annotation->offset.offset > offset) {
            continue;
        }
        if (closestOffset != UT64_MAX && closestOffset >= annotation->offset.offset) {
            continue;
        }
        closestPos = annotation->start;
        closestOffset = annotation->offset.offset;
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
    void *iter;
    r_vector_foreach(&codeDecompiled.annotations, iter)
    {
        RCodeMetaItem *annotation = (RCodeMetaItem *) iter;
        if (annotation->type != R_CODEMETA_TYPE_OFFSET) {
            continue;
        }
        if ((startPos <= annotation->start && annotation->start < endPos)
            || (startPos < annotation->end && annotation->end < endPos)) {
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
    if (addressInRange(addr)) {
        doRefresh();
    }
}

void DecompilerWidget::doRefresh()
{
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

    // Disabling decompiler selection combo box and making progress label
    // visible ahead of decompilation.
    ui->progressLabel->setVisible(true);
    ui->cancelButton->setVisible(true);
    // per user request: never disable the decompiler selection combobox

    // If there is a previous background decompilation running, interrupt it.
    if (currentDecompileTask && currentDecompileTask->isRunning()) {
        currentDecompileTask->interrupt();
        // we'll continue and start a new one; the previous will be cleaned up by the
        // AsyncTaskManager when finished.
    }

    // Clear all selections since we just refreshed
    ui->textEdit->setExtraSelections({});
    previousFunctionAddr = decompiledFunctionAddr;
    decompiledFunctionAddr = Core()->getFunctionStart(addr);
    updateWindowTitle();
    if (decompiledFunctionAddr == RVA_INVALID) {
        // No function was found, so making the progress label invisible and
        // enabling the decompiler selection combo box as we are not waiting for
        // any decompilation to finish.
        ui->progressLabel->setVisible(false);
        ui->cancelButton->setVisible(false);
        ui->decompilerComboBox->setEnabled(true);
        setCode(Decompiler::makeWarning(
            tr("No function found at this offset. "
               "Seek to a function or define one in order to decompile it.")));
        return;
    }

    mCtxMenu->setDecompiledFunctionAddress(decompiledFunctionAddr);

    // Create and start a background decompile task. When it finishes, collect the
    // results and update the UI.
    DecompileTask *task = new DecompileTask(dec, addr);
    AsyncTask::Ptr taskPtr(task);
    currentDecompileTask = taskPtr;
    decompilerBusy = true;

    // Use a queued connection so the slot/lambda runs in the main (GUI) thread.
    connect(
        task,
        &AsyncTask::finished,
        this,
        [this, task]() {
            ui->progressLabel->setVisible(false);
            ui->cancelButton->setVisible(false);

            RCodeMeta *cm = task->getCode();
            // Clear currentDecompileTask before calling decompilationFinished so
            // re-entrant refreshes behave correctly.
            currentDecompileTask.clear();
            decompilerBusy = false;
            if (cm) {
                this->decompilationFinished(cm);
            } else {
                this->decompilationFinished(Decompiler::makeWarning(
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
    if (pos == UT64_MAX || pos == 0) {
        return QTextCursor();
    }
    QTextCursor cursor = ui->textEdit->textCursor();
    cursor.setPosition(pos);
    return cursor;
}

void DecompilerWidget::decompilationFinished(RCodeMeta *codeDecompiled)
{
    bool isDisplayReset = false;
    if (previousFunctionAddr == decompiledFunctionAddr) {
        scrollerHorizontal = ui->textEdit->horizontalScrollBar()->sliderPosition();
        scrollerVertical = ui->textEdit->verticalScrollBar()->sliderPosition();
        isDisplayReset = true;
    }

    ui->progressLabel->setVisible(false);
    // per user request: never disable the decompiler selection combobox

    mCtxMenu->setAnnotationHere(nullptr);
    setCode(codeDecompiled);

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
    highlightPC();
    highlightBreakpoints();
    lowestOffsetInCode = RVA_MAX;
    highestOffsetInCode = 0;
    void *iter;
    r_vector_foreach(&code->annotations, iter)
    {
        RCodeMetaItem *annotation = (RCodeMetaItem *) iter;
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
    void *iter;
    r_vector_foreach(&this->code->annotations, iter)
    {
        RCodeMetaItem *annotation = (RCodeMetaItem *) iter;
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
    doRefresh();
}

void DecompilerWidget::cancelDecompilation()
{
    if (currentDecompileTask && currentDecompileTask->isRunning()) {
        currentDecompileTask->interrupt();
        ui->cancelButton->setVisible(false);
        ui->progressLabel->setText(tr("Cancelling..."));
    }
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
    // Do not perform seeks along with the cursor while selecting multiple lines
    if (!ui->textEdit->textCursor().selectedText().isEmpty()) {
        return;
    }

    size_t pos = ui->textEdit->textCursor().position();
    setAnnotationsAtCursor(pos);
    setInfoForBreakpoints();

    RVA offset = offsetForPosition(pos);
    if (offset != RVA_INVALID && offset != seekable->getOffset()) {
        seekFromCursor = true;
        seekable->seek(offset);
        mCtxMenu->setOffset(offset);
        seekFromCursor = false;
    }
    updateSelection();
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

    ui->textEdit->setExtraSelections(extraSelections);
    // Highlight PC after updating the selected line
    highlightPC();
}

QString DecompilerWidget::getWindowTitle() const
{
    RAnalFunction *fcn = Core()->functionAt(decompiledFunctionAddr);
    QString windowTitle = tr("Decompiler");
    if (fcn != NULL) {
        windowTitle += " (" + QString(fcn->name) + ")";
    } else {
        windowTitle += " (Empty)";
    }
    return windowTitle;
}

void DecompilerWidget::fontsUpdatedSlot()
{
    setupFonts();
}

void DecompilerWidget::colorsUpdatedSlot()
{
    bool useAnotationHiglighter = Config()->isDecompilerAnnotationHighlighterEnabled();
    if (useAnotationHiglighter != usingAnnotationBasedHighlighting) {
        setHighlighter(useAnotationHiglighter);
    }
}

void DecompilerWidget::showDecompilerContextMenu(const QPoint &pt)
{
    mCtxMenu->exec(ui->textEdit->mapToGlobal(pt));
}

void DecompilerWidget::seekToReference()
{
    size_t pos = ui->textEdit->textCursor().position();
    RVA offset = offsetForPosition(pos);
    seekable->seekToReference(offset);
}

bool DecompilerWidget::eventFilter(QObject *obj, QEvent *event)
{
    // Handle Vim-like mark and jump in decompiler view
    if (event->type() == QEvent::KeyPress
        && (obj == ui->textEdit || obj == ui->textEdit->viewport())) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
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
        ui->textEdit->setTextCursor(ui->textEdit->cursorForPosition(mouseEvent->pos()));
        seekToReference();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress
        && (obj == ui->textEdit || obj == ui->textEdit->viewport())) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton && !ui->textEdit->textCursor().hasSelection()) {
            ui->textEdit->setTextCursor(ui->textEdit->cursorForPosition(mouseEvent->pos()));
            size_t pos = ui->textEdit->textCursor().position();
            this->currentOffset = offsetForPosition(pos);
            return true;
        }
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
    ui->textEdit->setExtraSelections(extraSelections);
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

    void *iter;
    r_vector_foreach(&code.annotations, iter)
    {
        RCodeMetaItem *annotation = (RCodeMetaItem *) iter;
        annotation->start = mapPos(annotation->start);
        annotation->end = mapPos(annotation->end);
    }
#endif
    return text;
}

void DecompilerWidget::setCode(RCodeMeta *code)
{
    connectCursorPositionChanged(false);
    if (auto highlighter = qobject_cast<DecompilerHighlighter *>(syntaxHighlighter.get())) {
        highlighter->setAnnotations(code);
    }
    this->code.reset(code);
    QString text = remapAnnotationOffsetsToQString(*this->code);
    this->ui->textEdit->setPlainText(text);
    connectCursorPositionChanged(true);
    syntaxHighlighter->rehighlight();
}

void DecompilerWidget::setHighlighter(bool annotationBasedHighlighter)
{
    usingAnnotationBasedHighlighting = annotationBasedHighlighter;
    if (usingAnnotationBasedHighlighting) {
        syntaxHighlighter.reset(new DecompilerHighlighter());
        static_cast<DecompilerHighlighter *>(syntaxHighlighter.get())->setAnnotations(code.get());
    } else {
        syntaxHighlighter.reset(Config()->createSyntaxHighlighter(nullptr));
    }
    syntaxHighlighter->setDocument(ui->textEdit->document());
}
