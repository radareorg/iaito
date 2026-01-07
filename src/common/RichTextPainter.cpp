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
 * Performance-optimized rich text painting using QTextLayout
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

    // Build a single text string and collect format information
    QString fullText;
    int totalLength = 0;
    for (const auto &rt : richText) {
        totalLength += rt.text.length();
    }
    fullText.reserve(totalLength);

    std::vector<std::pair<int, int>> textRanges; // (start, length) pairs for each rich text element
    std::vector<QTextCharFormat> formats; // formats for each rich text element
    std::vector<T> textWidths; // widths for each text segment (for highlights)

    for (const CustomRichText_t &curRichText : richText) {
        int startPos = fullText.length();
        fullText += curRichText.text;
        int length = curRichText.text.length();
        textRanges.push_back({startPos, length});
        textWidths.push_back(fontMetrics->width(curRichText.text));

        // Create QTextCharFormat for this text segment
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

        if (curRichText.highlight && curRichText.highlightColor.alpha()) {
            format.setFontUnderline(true);
            format.setUnderlineColor(curRichText.highlightColor);
            format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        }

        formats.push_back(format);
    }

    // Calculate the visible portion of text
    T visibleWidth = w - xinc;
    if (visibleWidth <= 0)
        return;

    // Find the substring that fits within the visible width
    int visibleEnd = fullText.length();
    T currentWidth = 0;

    for (size_t i = 0; i < richText.size(); ++i) {
        T segmentWidth = textWidths[i];
        if (currentWidth + segmentWidth > visibleWidth) {
            // This segment goes beyond the visible area
            // Find how many characters fit
            const QString &text = richText[i].text;
            T charWidth = 0;
            int charsThatFit = 0;
            for (int j = 0; j < text.length(); ++j) {
                T nextCharWidth = fontMetrics->width(text[j]);
                if (currentWidth + charWidth + nextCharWidth > visibleWidth) {
                    break;
                }
                charWidth += nextCharWidth;
                charsThatFit++;
            }

            if (charsThatFit > 0) {
                // Truncate this text segment
                fullText = fullText.left(textRanges[i].first + charsThatFit);
                visibleEnd = fullText.length();
                // Update the range for the truncated segment
                textRanges[i].second = charsThatFit;
            } else {
                // No characters from this segment fit, remove it entirely
                fullText = fullText.left(textRanges[i].first);
                visibleEnd = fullText.length();
                textRanges.resize(i);
                formats.resize(i);
            }
            break;
        }
        currentWidth += segmentWidth;
    }

    if (fullText.isEmpty())
        return;

    // Create QTextLayout for efficient text rendering
    QTextLayout layout(fullText, painter->font());

    // Create a vector of format ranges
    QVector<QTextLayout::FormatRange> formatRanges;
    for (size_t i = 0; i < textRanges.size(); ++i) {
        const auto &range = textRanges[i];
        const auto &format = formats[i];
        if (range.first < visibleEnd) { // Only format visible text
            int visibleLength = qMin(range.second, visibleEnd - range.first);
            QTextLayout::FormatRange fmtRange;
            fmtRange.start = range.first;
            fmtRange.length = visibleLength;
            fmtRange.format = format;
            formatRanges.append(fmtRange);
        }
    }

    // Set up the layout with format ranges
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid()) {
        line.setLineWidth(visibleWidth);
        line.setPosition(QPointF(0, 0));
    }
    layout.endLayout();

    // Draw the layout at the specified position with format ranges
    layout.draw(painter, QPointF(x + xinc, y), formatRanges);

    // Draw highlight lines manually (QTextLayout underline might not be sufficient)
    T currentX = x + xinc;
    for (size_t i = 0; i < richText.size() && currentX < x + w; ++i) {
        const auto &curRichText = richText[i];
        T segmentWidth = textWidths[i];

        if (curRichText.highlight && curRichText.highlightColor.alpha()) {
            if (currentX + segmentWidth > x + w) {
                segmentWidth = (x + w) - currentX;
            }
            if (segmentWidth > 0) {
                QPen highlightPen(curRichText.highlightColor);
                highlightPen.setWidth(curRichText.highlightWidth);
                painter->setPen(highlightPen);
                T highlightOffsetX = curRichText.highlightConnectPrev ? -1 : 1;
                painter->drawLine(
                    currentX + highlightOffsetX, y + h - 1,
                    currentX + segmentWidth - 1, y + h - 1);
            }
        }

        currentX += segmentWidth;
        if (currentX >= x + w)
            break;
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
