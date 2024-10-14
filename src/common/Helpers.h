#ifndef QHELPERS_H
#define QHELPERS_H

#include "core/IaitoCommon.h"

#include <functional>
#include <QColor>
#include <QSizePolicy>
#include <QString>

class QIcon;
class QPlainTextEdit;
class QTextEdit;
class QString;
class QTreeWidget;
class QTreeWidgetItem;
class QAbstractItemView;
class QAbstractItemModel;
class QAbstractButton;
class QWidget;
class QTreeView;
class QAction;
class QMenu;
class QPaintDevice;
class QComboBox;

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
#define IAITO_QT_SKIP_EMPTY_PARTS QString::SkipEmptyParts
#else
#define IAITO_QT_SKIP_EMPTY_PARTS Qt::SkipEmptyParts
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define FILTER_REGEX this->filterRegularExpression()
#else
#define FILTER_REGEX filterRegExp()
#endif

RCore *iaitoPluginCore(void);
namespace qhelpers {
IAITO_EXPORT QString formatBytecount(const uint64_t bytecount);
IAITO_EXPORT void adjustColumns(QTreeView *tv, int columnCount, int padding);
IAITO_EXPORT void adjustColumns(QTreeWidget *tw, int padding);
IAITO_EXPORT bool selectFirstItem(QAbstractItemView *itemView);
IAITO_EXPORT QTreeWidgetItem *appendRow(
    QTreeWidget *tw,
    const QString &str,
    const QString &str2 = QString(),
    const QString &str3 = QString(),
    const QString &str4 = QString(),
    const QString &str5 = QString());

IAITO_EXPORT void setVerticalScrollMode(QAbstractItemView *tw);

IAITO_EXPORT void setCheckedWithoutSignals(QAbstractButton *button, bool checked);

struct IAITO_EXPORT SizePolicyMinMax
{
    QSizePolicy sizePolicy;
    int min;
    int max;

    void restoreWidth(QWidget *widget);
    void restoreHeight(QWidget *widget);
};

IAITO_EXPORT SizePolicyMinMax forceWidth(QWidget *widget, int width);
IAITO_EXPORT SizePolicyMinMax forceHeight(QWidget *widget, int height);

IAITO_EXPORT int getMaxFullyDisplayedLines(QTextEdit *textEdit);
IAITO_EXPORT int getMaxFullyDisplayedLines(QPlainTextEdit *plainTextEdit);

IAITO_EXPORT QByteArray applyColorToSvg(const QByteArray &data, QColor color);
IAITO_EXPORT QByteArray applyColorToSvg(const QString &filename, QColor color);

IAITO_EXPORT void setThemeIcons(
    QList<QPair<void *, QString>> supportedIconsNames,
    std::function<void(void *, const QIcon &)> setter);

IAITO_EXPORT void prependQAction(QAction *action, QMenu *menu);
IAITO_EXPORT qreal devicePixelRatio(const QPaintDevice *p);
/**
 * @brief Select comboBox item by value in Qt::UserRole.
 * @param comboBox
 * @param data - value to search in combobox item data
 * @param defaultIndex - item to select in case no match
 */
IAITO_EXPORT void selectIndexByData(QComboBox *comboBox, QVariant data, int defaultIndex = -1);
/**
 * @brief Emit data change signal in a model's column (DisplayRole)
 * @param model - model containing data with changes
 * @param column - column in the model
 */
IAITO_EXPORT void emitColumnChanged(QAbstractItemModel *model, int column);

} // namespace qhelpers

#endif // HELPERS_H
