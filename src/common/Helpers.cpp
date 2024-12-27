#include "common/Helpers.h"
#include "Configuration.h"

#include <cmath>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDockWidget>
#include <QFileInfo>
#include <QMenu>
#include <QPlainTextEdit>
#include <QString>
#include <QTextEdit>
#include <QTreeWidget>
#include <QtCore>

static QAbstractItemView::ScrollMode scrollMode()
{
    const bool use_scrollperpixel = true;
    return use_scrollperpixel ? QAbstractItemView::ScrollPerPixel
                              : QAbstractItemView::ScrollPerItem;
}

RCore *iaitoPluginCore(void)
{
    char *p = r_sys_getenv("R2COREPTR");
    if (R_STR_ISNOTEMPTY(p)) {
        RCore *k = (RCore *) (void *) (size_t) r_num_get(NULL, p);
        free(p);
        return k;
    }
    return nullptr;
}

namespace qhelpers {

QString formatBytecount(const uint64_t bytecount)
{
    if (bytecount == 0) {
        return "0";
    }

    const int exp = log(bytecount) / log(1024);
    constexpr char suffixes[] = {' ', 'k', 'M', 'G', 'T', 'P', 'E'};

    QString str;
    QTextStream stream(&str);
    stream << qSetRealNumberPrecision(3) << bytecount / pow(1024, exp) << ' ' << suffixes[exp]
           << 'B';
    return stream.readAll();
}

void adjustColumns(QTreeView *tv, int columnCount, int padding)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    for (int i = 0; i < columnCount; i++) {
        tv->resizeColumnToContents(i);
        if (padding > 0) {
            int width = tv->columnWidth(i);
            tv->setColumnWidth(i, width + padding);
        }
    }
#endif
}

void adjustColumns(QTreeWidget *tw, int padding)
{
    adjustColumns(tw, tw->columnCount(), padding);
}

QTreeWidgetItem *appendRow(
    QTreeWidget *tw,
    const QString &str,
    const QString &str2,
    const QString &str3,
    const QString &str4,
    const QString &str5)
{
    QTreeWidgetItem *tempItem = new QTreeWidgetItem();
    // Fill dummy hidden column
    tempItem->setText(0, "0");
    tempItem->setText(1, str);
    if (!str2.isNull())
        tempItem->setText(2, str2);
    if (!str3.isNull())
        tempItem->setText(3, str3);
    if (!str4.isNull())
        tempItem->setText(4, str4);
    if (!str5.isNull())
        tempItem->setText(5, str5);

    tw->insertTopLevelItem(0, tempItem);

    return tempItem;
}

/**
 * @brief Select first item of a QAbstractItemView if not empty
 * @param itemView
 * @return true if first item was selected
 */
bool selectFirstItem(QAbstractItemView *itemView)
{
    if (!itemView) {
        return false;
    }
    auto model = itemView->model();
    if (!model) {
        return false;
    }
    if (model->hasIndex(0, 0)) {
        itemView->setCurrentIndex(model->index(0, 0));
        return true;
    }
    return false;
}

void setVerticalScrollMode(QAbstractItemView *tw)
{
    tw->setVerticalScrollMode(scrollMode());
}

void setCheckedWithoutSignals(QAbstractButton *button, bool checked)
{
    bool blocked = button->signalsBlocked();
    button->blockSignals(true);
    button->setChecked(checked);
    button->blockSignals(blocked);
}

SizePolicyMinMax forceWidth(QWidget *widget, int width)
{
    SizePolicyMinMax r;
    r.sizePolicy = widget->sizePolicy();
    r.min = widget->minimumWidth();
    r.max = widget->maximumWidth();

    QSizePolicy sizePolicy = r.sizePolicy;
    sizePolicy.setHorizontalPolicy(QSizePolicy::Fixed);
    widget->setSizePolicy(sizePolicy);
    widget->setMinimumWidth(width);
    widget->setMaximumWidth(width);

    return r;
}

SizePolicyMinMax forceHeight(QWidget *widget, int height)
{
    SizePolicyMinMax r;
    r.sizePolicy = widget->sizePolicy();
    r.min = widget->minimumHeight();
    r.max = widget->maximumHeight();

    QSizePolicy sizePolicy = r.sizePolicy;
    sizePolicy.setVerticalPolicy(QSizePolicy::Fixed);
    widget->setSizePolicy(sizePolicy);
    widget->setMinimumHeight(height);
    widget->setMaximumHeight(height);

    return r;
}

void SizePolicyMinMax::restoreWidth(QWidget *widget)
{
    widget->setSizePolicy(sizePolicy.horizontalPolicy(), widget->sizePolicy().verticalPolicy());
    widget->setMinimumWidth(min);
    widget->setMaximumWidth(max);
}

void SizePolicyMinMax::restoreHeight(QWidget *widget)
{
    widget->setSizePolicy(widget->sizePolicy().horizontalPolicy(), sizePolicy.verticalPolicy());
    widget->setMinimumHeight(min);
    widget->setMaximumHeight(max);
}

int getMaxFullyDisplayedLines(QTextEdit *textEdit)
{
    QFontMetrics fontMetrics(textEdit->document()->defaultFont());
    return (textEdit->height()
            - (textEdit->contentsMargins().top() + textEdit->contentsMargins().bottom()
               + (int) (textEdit->document()->documentMargin() * 2)))
           / fontMetrics.lineSpacing();
}

int getMaxFullyDisplayedLines(QPlainTextEdit *plainTextEdit)
{
    QFontMetrics fontMetrics(plainTextEdit->document()->defaultFont());
    return (plainTextEdit->height()
            - (plainTextEdit->contentsMargins().top() + plainTextEdit->contentsMargins().bottom()
               + (int) (plainTextEdit->document()->documentMargin() * 2)))
           / fontMetrics.lineSpacing();
}

QByteArray applyColorToSvg(const QByteArray &data, QColor color)
{
    static const QRegularExpression styleRegExp(
        "(?:style=\".*fill:(.*?);.*?\")|(?:fill=\"(.*?)\")");

    QString replaceStr = QStringLiteral("#%1").arg(color.rgb() & 0xffffff, 6, 16, QLatin1Char('0'));
    int replaceStrLen = replaceStr.length();

    QString xml = QString::fromUtf8(data);

    int offset = 0;
    while (true) {
        QRegularExpressionMatch match = styleRegExp.match(xml, offset);
        if (!match.hasMatch()) {
            break;
        }

        int captureIndex = match.captured(1).isNull() ? 2 : 1;
        xml.replace(match.capturedStart(captureIndex), match.capturedLength(captureIndex), replaceStr);
        offset = match.capturedStart(captureIndex) + replaceStrLen;
    }

    return xml.toUtf8();
}

QByteArray applyColorToSvg(const QString &filename, QColor color)
{
    QFile file(filename);
    file.open(QIODevice::ReadOnly);

    return applyColorToSvg(file.readAll(), color);
}

/**
 * @brief finds the theme-specific icon path and calls `setter` functor
 * providing a pointer of an object which has to be used and loaded icon
 * @param supportedIconsNames list of <object ptr, icon name>
 * @param setter functor which has to be called
 *   for example we need to set an action icon, the functor can be just [](void*
 * o, const QIcon &icon) { static_cast<QAction*>(o)->setIcon(icon); }
 */
void setThemeIcons(
    QList<QPair<void *, QString>> supportedIconsNames,
    std::function<void(void *, const QIcon &)> setter)
{
    if (supportedIconsNames.isEmpty() || !setter) {
        return;
    }

    const QString &iconsDirPath = QStringLiteral(":/img/icons/");
    const QString &currTheme = Config()->getCurrentTheme()->name;
    const QString &relativeThemeDir = currTheme.toLower() + "/";
    QString iconPath;

    foreach (const auto &p, supportedIconsNames) {
        iconPath = iconsDirPath;
        // Verify that indeed there is an alternative icon in this theme
        // Otherwise, fallback to the regular icon folder
        if (QFile::exists(iconPath + relativeThemeDir + p.second)) {
            iconPath += relativeThemeDir;
        }
        setter(p.first, QIcon(iconPath + p.second));
    }
}

void prependQAction(QAction *action, QMenu *menu)
{
    auto actions = menu->actions();
    if (actions.empty()) {
        menu->addAction(action);
    } else {
        menu->insertAction(actions.first(), action);
    }
}

qreal devicePixelRatio(const QPaintDevice *p)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    return p->devicePixelRatioF();
#else
    return p->devicePixelRatio();
#endif
}

void selectIndexByData(QComboBox *widget, QVariant data, int defaultIndex)
{
    for (int i = 0; i < widget->count(); i++) {
        if (widget->itemData(i) == data) {
            widget->setCurrentIndex(i);
            return;
        }
    }
    widget->setCurrentIndex(defaultIndex);
}

void emitColumnChanged(QAbstractItemModel *model, int column)
{
    if (model && model->rowCount()) {
        emit model->dataChanged(
            model->index(0, column), model->index(model->rowCount() - 1, column), {Qt::DisplayRole});
    }
}

} // namespace qhelpers
