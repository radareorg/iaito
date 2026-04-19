#ifndef PINPICKERMENU_H
#define PINPICKERMENU_H

#include "core/IaitoCommon.h"
#include <QMenu>
#include <QString>

class IAITO_EXPORT PinPickerMenu : public QMenu
{
    Q_OBJECT
public:
    explicit PinPickerMenu(const QString &title, QWidget *parent = nullptr);

signals:
    void pinPicked(const QString &emoji);

private:
    void addPreset(const QString &emoji, const QString &name);
    void addCustom();
    void addReset();
};

#endif // PINPICKERMENU_H
