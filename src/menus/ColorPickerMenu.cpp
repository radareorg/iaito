#include "ColorPickerMenu.h"

#include <QAction>
#include <QColorDialog>
#include <QPainter>
#include <QPixmap>

static QIcon swatchIcon(const QColor &color)
{
    const int s = 16;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(1, 1, s - 2, s - 2, color);
    p.setPen(QColor(0, 0, 0, 160));
    p.drawRect(0, 0, s - 1, s - 1);
    return QIcon(pm);
}

ColorPickerMenu::ColorPickerMenu(const QString &title, QWidget *parent)
    : QMenu(title, parent)
{
    addSwatch(QStringLiteral("red"), QColor(0xff, 0x00, 0x00));
    addSwatch(QStringLiteral("orange"), QColor(0xff, 0x80, 0x00));
    addSwatch(QStringLiteral("yellow"), QColor(0xff, 0xff, 0x00));
    addSwatch(QStringLiteral("green"), QColor(0x00, 0xff, 0x00));
    addSwatch(QStringLiteral("cyan"), QColor(0x00, 0xff, 0xff));
    addSwatch(QStringLiteral("blue"), QColor(0x00, 0x00, 0xff));
    addSwatch(QStringLiteral("magenta"), QColor(0xff, 0x00, 0xff));
    addSwatch(QStringLiteral("brown"), QColor(0x80, 0x40, 0x00));
    addSwatch(QStringLiteral("gray"), QColor(0x80, 0x80, 0x80));
    addSwatch(QStringLiteral("white"), QColor(0xff, 0xff, 0xff));
    addSwatch(QStringLiteral("black"), QColor(0x00, 0x00, 0x00));
    addSeparator();
    addCustom();
    addReset();
}

void ColorPickerMenu::addSwatch(const QString &r2Name, const QColor &color)
{
    QAction *a = addAction(swatchIcon(color), r2Name);
    connect(a, &QAction::triggered, this, [this, r2Name]() { emit colorPicked(r2Name); });
}

void ColorPickerMenu::addCustom()
{
    QAction *a = addAction(tr("Custom RGB..."));
    connect(a, &QAction::triggered, this, [this]() {
        QColor c = QColorDialog::getColor(Qt::white, this, tr("Pick color"));
        if (c.isValid()) {
            emit colorPicked(QStringLiteral("#%1%2%3")
                                 .arg(c.red(), 2, 16, QLatin1Char('0'))
                                 .arg(c.green(), 2, 16, QLatin1Char('0'))
                                 .arg(c.blue(), 2, 16, QLatin1Char('0')));
        }
    });
}

void ColorPickerMenu::addReset()
{
    QAction *a = addAction(tr("Reset"));
    connect(a, &QAction::triggered, this, [this]() { emit colorPicked(QString()); });
}
