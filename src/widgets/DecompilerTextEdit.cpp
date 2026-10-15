#include "DecompilerTextEdit.h"

#include <algorithm>
#include <QAbstractTextDocumentLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOption>
#include <QTextBlock>

DecompilerTextEdit::DecompilerTextEdit(QWidget *parent)
    : QPlainTextEdit(parent)
    , foldArea(new QWidget(this))
{
    setLineWrapMode(QPlainTextEdit::NoWrap);
    foldArea->setObjectName(QStringLiteral("decompilerFoldArea"));
    foldArea->setAccessibleName(tr("Code folding"));
    foldArea->setToolTip(tr("Click an arrow to collapse or expand a { ... } block"));
    foldArea->setMouseTracking(true);
    foldArea->installEventFilter(this);
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (foldArea->height() != viewport()->height()) {
            updateFoldAreaGeometry();
        }
        if (dy) {
            foldArea->scroll(0, dy);
        } else {
            foldArea->update(0, rect.y(), foldArea->width(), rect.height());
        }
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &DecompilerTextEdit::revealCursor);
    updateFoldAreaGeometry();
}

void DecompilerTextEdit::setDecompiledCode(
    const QString &text, const QString &source, bool preserveFolds)
{
    QScopedValueRollback<bool> guard(updatingCode, true);
    QString normalizedSource = source;
    normalizedSource.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalizedSource.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    preserveFolds = preserveFolds && normalizedSource == foldingSource;
    const QTextCursor previous = textCursor();
    const int previousLine = previous.blockNumber();
    const QString sourceLine = foldingSource.section(QLatin1Char('\n'), previousLine, previousLine);
    const int sourceColumn
        = qMax(0, previous.positionInBlock() - (previous.block().text().size() - sourceLine.size()));
    QSet<QPair<int, int>> collapsed;
    if (preserveFolds) {
        for (const FoldRegion &region : regions) {
            if (region.collapsed) {
                collapsed.insert({region.first, region.last});
            }
        }
    }
    regions.clear();
    setPlainText(text);
    foldingSource = normalizedSource;
    findFoldRegions(normalizedSource);
    for (FoldRegion &region : regions) {
        region.collapsed = collapsed.contains({region.first, region.last});
    }
    if (preserveFolds) {
        const QTextBlock block = document()->findBlockByNumber(previousLine);
        if (block.isValid()) {
            const int prefix = block.text().size() - sourceLine.size();
            QTextCursor cursor(block);
            cursor.setPosition(
                block.position() + qBound(0, prefix + sourceColumn, block.length() - 1));
            setTextCursor(cursor);
        }
    }
    applyFolding();
}

void DecompilerTextEdit::findFoldRegions(const QString &source)
{
    enum State { Code, LineComment, BlockComment, Quoted, RawString } state = Code;
    QVector<QPair<int, int>> openings;
    matchingBraces.clear();
    // Translate source positions past optional address and disassembly columns.
    QVector<int> lineOffsets;
    QTextBlock block = document()->begin();
    int sourcePosition = 0;
    for (const QString &text : source.split(QLatin1Char('\n'))) {
        lineOffsets.append(block.position() + block.text().size() - text.size() - sourcePosition);
        sourcePosition += text.size() + 1;
        block = block.next();
    }
    QChar quote;
    QString rawEnd;
    int line = 0;
    for (int i = 0; i < source.size(); ++i) {
        const QChar c = source.at(i);
        const QChar next = i + 1 < source.size() ? source.at(i + 1) : QChar();
        if (c == QLatin1Char('\n')) {
            ++line;
        }
        if (state == LineComment) {
            if (c == QLatin1Char('\n') && (i == 0 || source.at(i - 1) != QLatin1Char('\\'))) {
                state = Code;
            }
        } else if (state == BlockComment) {
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                state = Code;
                ++i;
            }
        } else if (state == Quoted) {
            if (c == QLatin1Char('\\') && !next.isNull()) {
                if (next == QLatin1Char('\n')) {
                    ++line;
                }
                ++i;
            } else if (c == quote) {
                state = Code;
            }
        } else if (state == RawString) {
            if (source.mid(i, rawEnd.size()) == rawEnd) {
                i += rawEnd.size() - 1;
                state = Code;
            }
        } else if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            state = LineComment;
            ++i;
        } else if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            state = BlockComment;
            ++i;
        } else if (c == QLatin1Char('R') && next == QLatin1Char('"')) {
            const int length = source.mid(i + 2, 17).indexOf(QLatin1Char('('));
            const QString delimiter = source.mid(i + 2, qMax(0, length));
            if (length >= 0 && std::none_of(delimiter.cbegin(), delimiter.cend(), [](QChar c) {
                    return c.isSpace() || c == QLatin1Char('\\') || c == QLatin1Char(')');
                })) {
                rawEnd = QLatin1Char(')') + delimiter + QLatin1Char('"');
                i += length + 2;
                state = RawString;
            }
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            state = Quoted;
        } else if (c == QLatin1Char('{')) {
            openings.append({line, i + lineOffsets.at(line)});
        } else if (c == QLatin1Char('}') && !openings.isEmpty()) {
            const auto opening = openings.takeLast();
            const int closing = i + lineOffsets.at(line);
            matchingBraces.insert(opening.second, closing);
            matchingBraces.insert(closing, opening.second);
            if (line > opening.first + 1) {
                regions.append({opening.first, line});
            }
        }
    }
    std::sort(regions.begin(), regions.end(), [](const FoldRegion &a, const FoldRegion &b) {
        return a.first != b.first ? a.first < b.first : a.last > b.last;
    });
}

int DecompilerTextEdit::foldForLine(int line) const
{
    auto it = std::lower_bound(
        regions.cbegin(), regions.cend(), line, [](const FoldRegion &region, int line) {
            return region.first < line;
        });
    return it != regions.cend() && it->first == line ? int(it - regions.cbegin()) : -1;
}

bool DecompilerTextEdit::isFolded(int line) const
{
    const int index = foldForLine(line);
    return index >= 0 && regions.at(index).collapsed;
}

bool DecompilerTextEdit::isFoldable(int line) const
{
    return foldForLine(line) >= 0;
}

void DecompilerTextEdit::toggleFold(int line)
{
    const int index = foldForLine(line);
    if (index >= 0) {
        const bool collapsed = !regions.at(index).collapsed;
        // One gutter arrow controls all braces opened on the same line.
        for (int i = index; i < regions.size() && regions.at(i).first == line; ++i) {
            regions[i].collapsed = collapsed;
        }
        applyFolding();
    }
}

void DecompilerTextEdit::toggleFoldAtCursor()
{
    const int line = textCursor().blockNumber();
    int index = foldForLine(line);
    if (index < 0) {
        for (int i = 0; i < regions.size(); ++i) {
            if (regions.at(i).first < line && regions.at(i).last >= line) {
                index = i;
            }
        }
    }
    if (index >= 0) {
        toggleFold(regions.at(index).first);
    }
}

void DecompilerTextEdit::jumpToMatchingBrace()
{
    const QTextCursor cursor = textCursor();
    const auto match = matchingBraces.lowerBound(cursor.position());
    if (match == matchingBraces.cend()
        || match.key() >= cursor.block().position() + cursor.block().length() - 1) {
        return;
    }
    QTextCursor target(document());
    target.setPosition(match.value());
    setTextCursor(target);
    ensureCursorVisible();
}

void DecompilerTextEdit::collapseAll()
{
    for (FoldRegion &region : regions) {
        region.collapsed = true;
    }
    applyFolding();
}

void DecompilerTextEdit::expandAll()
{
    for (FoldRegion &region : regions) {
        region.collapsed = false;
    }
    applyFolding();
}

void DecompilerTextEdit::applyFolding()
{
    QVector<int> depth(blockCount() + 1, 0);
    for (const FoldRegion &region : regions) {
        if (region.collapsed && region.last < blockCount()) {
            ++depth[region.first + 1];
            --depth[region.last];
        }
    }
    int hidden = 0;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        hidden += depth.at(block.blockNumber());
        block.setVisible(hidden == 0);
        block.setLineCount(hidden ? 0 : 1);
    }
    QTextBlock cursorBlock = textCursor().block();
    if (!cursorBlock.isVisible()) {
        while (cursorBlock.isValid() && !cursorBlock.isVisible()) {
            cursorBlock = cursorBlock.previous();
        }
        QSignalBlocker blocker(this);
        setTextCursor(QTextCursor(cursorBlock));
    }
    document()->markContentsDirty(0, document()->characterCount());
    auto *layout = document()->documentLayout();
    emit layout->documentSizeChanged(layout->documentSize());
    viewport()->update();
    foldArea->update();
    emit foldingChanged();
}

void DecompilerTextEdit::revealCursor()
{
    if (updatingCode) {
        return;
    }
    const int line = textCursor().blockNumber();
    QSet<int> openings;
    for (const FoldRegion &region : regions) {
        if (region.collapsed && region.first < line && region.last > line) {
            openings.insert(region.first);
        }
    }
    if (!openings.isEmpty()) {
        for (FoldRegion &region : regions) {
            if (openings.contains(region.first)) {
                region.collapsed = false;
            }
        }
        applyFolding();
    }
}

void DecompilerTextEdit::updateFoldAreaGeometry()
{
    const int width = qMax(16, fontMetrics().height());
    if (viewportMargins().left() != width) {
        setViewportMargins(width, 0, 0, 0);
    }
    const QRect rect = viewport()->geometry();
    foldArea->setGeometry(contentsRect().left(), rect.top(), width, rect.height());
}

void DecompilerTextEdit::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    updateFoldAreaGeometry();
}

void DecompilerTextEdit::changeEvent(QEvent *event)
{
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        updateFoldAreaGeometry();
    }
    foldArea->update();
}

int DecompilerTextEdit::foldAt(const QPoint &point) const
{
    const QPoint viewportPoint = foldArea->mapTo(this, point) - viewport()->pos();
    const QTextCursor cursor = cursorForPosition(QPoint(0, viewportPoint.y()));
    const QRect rect = cursorRect(cursor);
    if (!cursor.block().isVisible() || viewportPoint.y() < rect.top()
        || viewportPoint.y() > rect.bottom()) {
        return -1;
    }
    return foldForLine(cursor.blockNumber());
}

bool DecompilerTextEdit::eventFilter(QObject *object, QEvent *event)
{
    if (object == foldArea) {
        if (event->type() == QEvent::Paint) {
            paintFoldArea();
            return true;
        }
        if (event->type() == QEvent::MouseMove) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            foldArea->setCursor(
                foldAt(mouse->pos()) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const int index = foldAt(mouse->pos());
            if (mouse->button() == Qt::LeftButton && index >= 0) {
                toggleFold(regions.at(index).first);
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick) {
            return true;
        }
    }
    return QPlainTextEdit::eventFilter(object, event);
}

void DecompilerTextEdit::paintFoldArea()
{
    QPainter painter(foldArea);
    painter.fillRect(foldArea->rect(), palette().alternateBase());
    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        if (!block.isVisible()) {
            continue;
        }
        const QRectF rect = blockBoundingGeometry(block).translated(contentOffset());
        if (rect.top() >= viewport()->height()) {
            break;
        }
        const int index = foldForLine(block.blockNumber());
        if (index < 0) {
            continue;
        }
        QStyleOption option;
        option.initFrom(foldArea);
        option.rect = QRect(
            0, qRound(rect.top()), foldArea->width(), qRound(blockBoundingRect(block).height()));
        option.state |= QStyle::State_Children;
        if (!regions.at(index).collapsed) {
            option.state |= QStyle::State_Open;
        }
        style()->drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, foldArea);
    }
}

void DecompilerTextEdit::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);
    QPainter painter(viewport());
    painter.setPen(palette().color(QPalette::Mid));
    for (const FoldRegion &region : regions) {
        if (!region.collapsed) {
            continue;
        }
        const QTextBlock block = document()->findBlockByNumber(region.first);
        if (!block.isVisible()) {
            continue;
        }
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::EndOfBlock);
        const QRect rect = cursorRect(cursor);
        if (rect.intersects(viewport()->rect())) {
            painter
                .drawText(rect.topLeft() + QPoint(3, fontMetrics().ascent()), QStringLiteral(" ..."));
        }
    }
}
