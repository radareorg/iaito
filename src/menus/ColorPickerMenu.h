#ifndef COLORPICKERMENU_H
#define COLORPICKERMENU_H

#include "core/IaitoCommon.h"
#include <QMenu>
#include <QString>

/**
 * Reusable submenu offering a row of basic-color swatches plus a "custom RGB"
 * action and a reset action. Emits colorPicked(str) where str is an r2-friendly
 * token: a named color ("red", ...), a "#RRGGBB" hex string, or "" to clear.
 */
class IAITO_EXPORT ColorPickerMenu : public QMenu
{
    Q_OBJECT
public:
    explicit ColorPickerMenu(const QString &title, QWidget *parent = nullptr);

signals:
    void colorPicked(const QString &r2Color);

private:
    void addSwatch(const QString &r2Name, const QColor &color);
    void addCustom();
    void addReset();
};

#endif // COLORPICKERMENU_H
