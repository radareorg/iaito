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
#include <QtGlobal>

/**
 * RichTextPainter implementation using QTextLayout for improved performance
 *
 * Key optimizations:
 * 1. QTextLayout determines visible text range - no per-char width calculations
 * 2. Single width computation per segment (eliminates duplicates)
 * 3. QTextLine::textLength() tells us exactly what fits
 * 4. Reuse QPen object for highlight drawing
 * 5. Process only visible segments for highlights
 * 6. Single formatRanges vector built once
 *
 * Performance characteristics:
 * - O(segments) width calculations (not O(characters))
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
    if (richText.empty() || xinc >= w)
        return;

    T visibleWidth = w - xinc;
    if (visibleWidth <= 0)
        return;

    // Pre-allocate vectors to avoid reallocations
    QVector<QTextLayout::FormatRange> formatRanges;
    formatRanges.reserve(richText.size());

    std::vector<T> segmentWidths;
    segmentWidths.reserve(richText.size());

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

        // Compute width once and store for later use
        T segmentWidth = fontMetrics->width(curRichText.text);
        segmentWidths.push_back(segmentWidth);

        // Build the full text
        fullText += curRichText.text;

        // Create format range for this segment
        QTextCharFormat format;
        switch (curRichText.flags) {
        case FlagNone:
            format.setForeground(QBrush(ConfigColor("btext")));
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

    // Determine how many segments are actually visible
    int visibleSegments = richText.size();
    T currentWidth = 0;
    for (size_t i = 0; i < richText.size(); ++i) {
        currentWidth += segmentWidths[i];
        if (currentWidth > visibleWidth) {
            visibleSegments = i + 1;
            break;
        }
    }

    // Handle highlightConnectPrev with optimized manual drawing
    T currentX = x + xinc;
    bool prevHighlighted = false;
    T highlightStartX = currentX;
    QPen highlightPen; // Reuse pen object

    for (int i = 0; i < visibleSegments && currentX < x + w; ++i) {
        const auto &curRichText = richText[i];
        T segmentWidth = segmentWidths[i];

        // Only handle connected highlights manually
        if (curRichText.highlight && curRichText.highlightColor.alpha() && curRichText.highlightConnectPrev) {
            if (!prevHighlighted) {
                highlightStartX = currentX;
            }
            prevHighlighted = true;
        } else if (prevHighlighted) {
            // Draw connected highlight line
            T highlightEndX = currentX;
            if (highlightEndX > highlightStartX) {
                highlightPen.setColor(curRichText.highlightColor);
                highlightPen.setWidth(curRichText.highlightWidth);
                painter->setPen(highlightPen);
                painter->drawLine(highlightStartX - 1, y + h - 1, highlightEndX - 1, y + h - 1);
            }
            prevHighlighted = false;
        }

        currentX += segmentWidth;
        if (currentX >= x + w)
            break;
    }

    // Draw final connected highlight if needed
    if (prevHighlighted) {
        T highlightEndX = qMin(currentX, x + w);
        if (highlightEndX > highlightStartX) {
            highlightPen.setColor(richText[visibleSegments - 1].highlightColor);
            highlightPen.setWidth(richText[visibleSegments - 1].highlightWidth);
            painter->setPen(highlightPen);
            painter->drawLine(highlightStartX - 1, y + h - 1, highlightEndX - 1, y + h - 1);
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
