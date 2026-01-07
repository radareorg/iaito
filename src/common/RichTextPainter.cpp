/* x64dbg RichTextPainter */
#include "RichTextPainter.h"
#include "CachedFontMetrics.h"
#include "common/Configuration.h"
#include <QPainter>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>

/**
 * RichTextPainter implementation using QTextLayout for improved performance
 *
 * Key optimizations:
 * 1. QTextLayout determines visible text range - no per-char width calculations
 * 2. Track segment boundaries (text indices) instead of computing segment widths
 * 3. Use QTextLine::cursorToX() for accurate highlight positions
 * 4. Pre-allocate all containers to avoid reallocations
 * 5. Pre-compute default text brush to avoid repeated ConfigColor lookups
 * 6. Track active highlight color/width to fix connected highlight drawing
 * 7. Early return on empty text to avoid unnecessary QTextLayout creation
 *
 * Performance characteristics:
 * - No per-segment width calculations (derived from QTextLine)
 * - Single ConfigColor lookup for default text color
 * - QTextLayout handles complex text shaping internally
 * - Single QTextLayout::draw() call for text rendering
 * - Minimal memory allocations with reserved vectors
 */
template<typename T>
void RichTextPainter::paintRichText(
    QPainter *painter,
    T x,
    T y,
    T w,
    T h,
    T xinc,
    const List &richText,
    CachedFontMetrics<T> *fontMetrics)
{
    Q_UNUSED(fontMetrics); // kept for backward compatibility with pre-QTextLayout API
    if (richText.empty() || xinc >= w)
        return;

    T visibleWidth = w - xinc;
    if (visibleWidth <= 0)
        return;

    // Pre-allocate vectors to avoid reallocations
    QVector<QTextLayout::FormatRange> formatRanges;
    formatRanges.reserve(richText.size());

    // Track segment boundaries (text indices) instead of widths
    std::vector<int> segmentBoundaries;
    segmentBoundaries.reserve(richText.size());

    // Pre-compute default text brush to avoid repeated ConfigColor lookups
    const QBrush defaultTextBrush(ConfigColor("btext"));

    QString fullText;
    int totalLength = 0;
    for (const auto &rt : richText) {
        totalLength += rt.text.length();
    }
    fullText.reserve(totalLength);

    int currentPos = 0;

    // Build text and format ranges in a single pass
    for (const CustomRichText_t &curRichText : richText) {
        int textLength = curRichText.text.length();

        // Record segment boundary
        segmentBoundaries.push_back(currentPos);

        // Build the full text
        fullText += curRichText.text;

        // Create format range for this segment
        QTextCharFormat format;
        switch (curRichText.flags) {
        case FlagNone:
            format.setForeground(defaultTextBrush);
            break;
        case FlagColor:
            format.setForeground(QBrush(curRichText.textColor));
            break;
        case FlagBackground:
            if (curRichText.textBackground.alpha()) {
                format.setBackground(QBrush(curRichText.textBackground));
            }
            break;
        case FlagAll:
            format.setForeground(QBrush(curRichText.textColor));
            if (curRichText.textBackground.alpha()) {
                format.setBackground(QBrush(curRichText.textBackground));
            }
            break;
        }

        // Use QTextLayout's underline for highlights
        if (curRichText.highlight && curRichText.highlightColor.alpha()) {
            format.setFontUnderline(true);
            format.setUnderlineColor(curRichText.highlightColor);
            format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        }

        QTextLayout::FormatRange fmtRange;
        fmtRange.start = currentPos;
        fmtRange.length = textLength;
        fmtRange.format = format;
        formatRanges.append(fmtRange);

        currentPos += textLength;
    }

    // Early return if no text to render
    if (fullText.isEmpty())
        return;

    // Create QTextLayout and let it determine what fits
    QTextLayout layout(fullText, painter->font());

    // Set up layout - QTextLayout will handle truncation automatically
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid()) {
        line.setLineWidth(visibleWidth);
        line.setPosition(QPointF(0, 0));
    }
    layout.endLayout();

    // Single draw call for visible text
    layout.draw(painter, QPointF(x + xinc, y), formatRanges);

    // Determine how many segments are actually visible using the layout
    int visibleSegments = richText.size();
    if (line.isValid()) {
        // Find segments that fall within the visible text range
        int visibleTextLength = line.textLength();
        for (size_t i = 0; i < segmentBoundaries.size(); ++i) {
            if (segmentBoundaries[i] >= visibleTextLength) {
                visibleSegments = i;
                break;
            }
        }
    }

    // Handle highlightConnectPrev using QTextLine for accurate positions
    T baseX = x + xinc;
    bool prevHighlighted = false;
    T highlightStartX = baseX;
    QColor activeHighlightColor;
    int activeHighlightWidth = 1;
    QPen highlightPen; // Reuse pen object

    for (int i = 0; i < visibleSegments; ++i) {
        const auto &curRichText = richText[i];
        int startIndex = segmentBoundaries[i];
        int endIndex = (i + 1 < (int)segmentBoundaries.size()) ? segmentBoundaries[i + 1] : fullText.length();

        // Calculate actual segment positions using QTextLine
        T segmentStartX = baseX;
        T segmentEndX = baseX;
        if (line.isValid()) {
            segmentStartX = baseX + line.cursorToX(startIndex);
            segmentEndX = baseX + line.cursorToX(endIndex);
            // Clamp to visible area
            segmentEndX = qMin(segmentEndX, x + w);
        }

        // Only handle connected highlights manually
        if (curRichText.highlight && curRichText.highlightColor.alpha() && curRichText.highlightConnectPrev) {
            if (!prevHighlighted) {
                highlightStartX = segmentStartX;
                activeHighlightColor = curRichText.highlightColor;
                activeHighlightWidth = curRichText.highlightWidth;
            }
            prevHighlighted = true;
        } else if (prevHighlighted) {
            // Draw connected highlight line using the active highlight properties
            T highlightEndX = segmentStartX;
            if (highlightEndX > highlightStartX) {
                highlightPen.setColor(activeHighlightColor);
                highlightPen.setWidth(activeHighlightWidth);
                painter->setPen(highlightPen);
                painter->drawLine(highlightStartX - 1, y + h - 1, highlightEndX - 1, y + h - 1);
            }
            prevHighlighted = false;
        }
    }

    // Draw final connected highlight if needed
    if (prevHighlighted && line.isValid()) {
        T finalSegmentEndX = qMin(baseX + line.cursorToX(line.textLength()), x + w);
        if (finalSegmentEndX > highlightStartX) {
            highlightPen.setColor(activeHighlightColor);
            highlightPen.setWidth(activeHighlightWidth);
            painter->setPen(highlightPen);
            painter->drawLine(highlightStartX - 1, y + h - 1, finalSegmentEndX - 1, y + h - 1);
        }
    }
}

template void RichTextPainter::paintRichText<qreal>(
    QPainter *painter,
    qreal x,
    qreal y,
    qreal w,
    qreal h,
    qreal xinc,
    const List &richText,
    CachedFontMetrics<qreal> *fontMetrics);

/**
 * @brief RichTextPainter::htmlRichText Convert rich text in x64dbg to HTML, for
 * use by other applications
 * @param richText The rich text to be converted to HTML format
 * @param textHtml The HTML source. Any previous content will be preserved and
 * new content will be appended at the end.
 * @param textPlain The plain text. Any previous content will be preserved and
 * new content will be appended at the end.
 */
void RichTextPainter::htmlRichText(const List &richText, QString &textHtml, QString &textPlain)
{
    for (const CustomRichText_t &curRichText : richText) {
        if (curRichText.text == " ") { // blank
            textHtml += " ";
            textPlain += " ";
            continue;
        }
        switch (curRichText.flags) {
        case FlagNone: // defaults
            textHtml += "<span>";
            break;
        case FlagColor: // color only
            textHtml
                += QStringLiteral("<span style=\"color:%1\">").arg(curRichText.textColor.name());
            break;
        case FlagBackground: // background only
            if (curRichText.textBackground
                != Qt::transparent) // QColor::name() returns "#000000" for
                                    // transparent color. That's not desired. Leave
                                    // it blank.
                textHtml += QStringLiteral("<span style=\"background-color:%1\">")
                                .arg(curRichText.textBackground.name());
            else
                textHtml += QStringLiteral("<span>");
            break;
        case FlagAll: // color+background
            if (curRichText.textBackground
                != Qt::transparent) // QColor::name() returns "#000000" for
                                    // transparent color. That's not desired. Leave
                                    // it blank.
                textHtml
                    += QStringLiteral("<span style=\"color:%1; background-color:%2\">")
                           .arg(curRichText.textColor.name(), curRichText.textBackground.name());
            else
                textHtml
                    += QStringLiteral("<span style=\"color:%1\">").arg(curRichText.textColor.name());
            break;
        }
        if (curRichText.highlight) // Underline highlighted token
            textHtml += "<u>";
        textHtml += curRichText.text.toHtmlEscaped();
        if (curRichText.highlight)
            textHtml += "</u>";
        textHtml += "</span>"; // Close the tag
        textPlain += curRichText.text;
    }
}

RichTextPainter::List RichTextPainter::fromTextDocument(const QTextDocument &doc)
{
    List r;

    for (QTextBlock block = doc.begin(); block != doc.end(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); it != block.end(); ++it) {
            QTextFragment fragment = it.fragment();
            QTextCharFormat format = fragment.charFormat();

            CustomRichText_t text;
            text.text = fragment.text();
            text.textColor = format.foreground().color();
            text.textBackground = format.background().color();

            bool hasForeground = format.hasProperty(QTextFormat::ForegroundBrush);
            bool hasBackground = format.hasProperty(QTextFormat::BackgroundBrush);

            if (hasForeground && !hasBackground) {
                text.flags = FlagColor;
            } else if (!hasForeground && hasBackground) {
                text.flags = FlagBackground;
            } else if (hasForeground && hasBackground) {
                text.flags = FlagAll;
            } else {
                text.flags = FlagNone;
            }

            r.push_back(text);
        }
    }

    return r;
}

RichTextPainter::List RichTextPainter::cropped(
    const RichTextPainter::List &richText, int maxCols, const QString &indicator, bool *croppedOut)
{
    List r;
    r.reserve(richText.size());

    int cols = 0;
    bool cropped = false;
    for (const auto &text : richText) {
        int textLength = text.text.size();
        if (cols + textLength <= maxCols) {
            r.push_back(text);
            cols += textLength;
        } else if (cols == maxCols) {
            break;
        } else {
            CustomRichText_t croppedText = text;
            croppedText.text.truncate(maxCols - cols);
            r.push_back(croppedText);
            cropped = true;
            break;
        }
    }

    if (cropped && !indicator.isEmpty()) {
        int indicatorCropLength = indicator.length();
        if (indicatorCropLength > maxCols) {
            indicatorCropLength = maxCols;
        }

        while (!r.empty()) {
            auto &text = r.back();

            if (text.text.length() >= indicatorCropLength) {
                text.text.replace(
                    text.text.length() - indicatorCropLength, indicatorCropLength, indicator);
                break;
            }

            indicatorCropLength -= text.text.length();
            r.pop_back();
        }
    }

    if (croppedOut) {
        *croppedOut = cropped;
    }
    return r;
}
