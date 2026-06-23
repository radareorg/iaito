#ifndef DISASSEMBLYWIDGET_H
#define DISASSEMBLYWIDGET_H

#include "MemoryDockWidget.h"
#include "common/CachedFontMetrics.h"
#include "common/IaitoSeekable.h"
#include "common/RefreshDeferrer.h"
#include "core/Iaito.h"

#include <QAction>
#include <QColor>
#include <QHash>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QTextEdit>

class DisassemblyTextEdit;
class DisassemblyScrollArea;
class DisassemblyContextMenu;
class DisassemblyLeftPanel;

class DisassemblyWidget : public MemoryDockWidget
{
    Q_OBJECT
public:
    explicit DisassemblyWidget(MainWindow *main);
    QSize minimumSizeHint() const override;
    QWidget *getTextWidget();

    static QString getWidgetType();

public slots:
    /**
     * @brief Highlights the currently selected line and updates the
     * highlighting of the same words under the cursor in the visible screen.
     * This overrides all previous highlighting.
     */
    void highlightCurrentLine();
    /**
     * @brief Adds the PC line highlighting to the other current highlighting.
     * This should be called after highlightCurrentLine since that function
     * overrides all previous highlighting.
     */
    void highlightPCLine();
    void showDisasContextMenu(const QPoint &pt);
    /**
     * @brief Slot to copy raw bytes of the selected instructions
     */
    void copyBytes();
    void fontsUpdatedSlot();
    void colorsUpdatedSlot();
    void scrollInstructions(int count);
    void seekPrev();
    void setPreviewMode(bool previewMode);
    QFontMetrics getFontMetrics();
    QList<DisassemblyLine> getLines();

protected slots:
    void on_seekChanged(RVA offset);
    void on_refreshContents();
    void refreshIfInRange(RVA offset);
    void refreshDisasm(RVA offset = RVA_INVALID);

    bool updateMaxLines();

    void cursorPositionChanged();

protected:
    DisassemblyContextMenu *mCtxMenu;
    DisassemblyScrollArea *mDisasScrollArea;
    DisassemblyTextEdit *mDisasTextEdit;
    DisassemblyLeftPanel *leftPanel;
    QList<DisassemblyLine> lines;

private:
    RVA topOffset;
    RVA bottomOffset;
    int maxLines;

    QString curHighlightedWord;

    struct BasicBlockColor
    {
        RVA start = RVA_INVALID;
        RVA end = RVA_INVALID;
        QColor color;
    };

    QList<BasicBlockColor> basicBlockColorCache;
    QHash<RVA, QList<XrefDescription>> outgoingXRefsCache;

    /**
     * offset of lines below the first line of the current seek
     */
    int cursorLineOffset;
    int cursorCharOffset;
    bool seekFromCursor;
    bool applyingAddressRangeSelection = false;
    bool publishingAddressRangeSelection = false;

    RefreshDeferrer *disasmRefresh;

    RVA readCurrentDisassemblyOffset();
    RVA readDisassemblyOffset(QTextCursor tc);
    bool getSelectedInstructionRange(RVA *startOffset, int *size);
    void publishAddressRangeSelection();
    void applyAddressRangeSelection(RVA start, RVA end);
    bool eventFilter(QObject *obj, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    QString getWindowTitle() const override;

    QList<RVA> breakpoints;

    void setupFonts();
    void setupColors();
    BasicBlockColor getBasicBlockColor(RVA offset);
    void clearBasicBlockColorCache();

    void updateCursorPosition();
    void updateContextMenuState();

    void connectCursorPositionChanged(bool disconnect);

    void moveCursorRelative(bool up, bool page);

    void jumpToOffsetUnderCursor(const QTextCursor &);
    QList<XrefDescription> getOutgoingXRefs(RVA offset);
    RVA xrefTargetForToken(RVA offset, const QString &token);
    bool isActionableTokenAt(const QPoint &pos);
    void updateDisassemblyCursor(const QPoint &pos, Qt::MouseButtons buttons);
    QString deepLinkAt(const QPoint &pos);
};

class DisassemblyScrollArea : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit DisassemblyScrollArea(QWidget *parent = nullptr);

signals:
    void scrollLines(int lines);
    void disassemblyResized();

protected:
    bool viewportEvent(QEvent *event) override;

private:
    void resetScrollBars();
};

class DisassemblyTextEdit : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit DisassemblyTextEdit(QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
        , lockScroll(false)
    {}

    void setLockScroll(bool lock) { this->lockScroll = lock; }

    qreal textOffset() const;

public:
signals:
    void refreshContents();

protected:
    bool viewportEvent(QEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    bool lockScroll;
};

/**
 * This class is used to draw the left pane of the disassembly
 * widget. Its goal is to draw proper arrows for the jumps of the disassembly.
 */
class DisassemblyLeftPanel : public QFrame
{
public:
    DisassemblyLeftPanel(DisassemblyWidget *disas);
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    DisassemblyWidget *disas;
};

#endif // DISASSEMBLYWIDGET_H
