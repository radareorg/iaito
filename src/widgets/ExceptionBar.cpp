#include "ExceptionBar.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>

ExceptionBar::ExceptionBar(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    navigator = new QToolButton(this);
    navigator->setAutoRaise(true);
    navigator->setPopupMode(QToolButton::InstantPopup);
    navigator->setMenu(new QMenu(navigator));
    navigator->setToolTip(tr("Navigate protected ranges and exception handlers (read-only)"));
    layout->addWidget(navigator);
    layout->addStretch();
    hide();
}

void ExceptionBar::addRegion(QMenu *menu, const ExceptionRegion &region)
{
    auto *entry = menu->addMenu(QStringLiteral("#%1 · %2").arg(region.index).arg(region.label()));
    entry->setToolTipsVisible(true);
    entry->menuAction()->setToolTip(region.description());
    const auto add = [this, entry](const QString &title, quint64 address) {
        auto *action = entry->addAction(QStringLiteral("%1 · 0x%2").arg(title).arg(address, 0, 16));
        connect(action, &QAction::triggered, this, [this, address]() {
            emit seekRequested(address);
        });
    };
    add(tr("Go to handler"), region.handler);
    add(tr("Go to try start"), region.from);
    add(tr("Go to try end (exclusive)"), region.to);
    // In PE metadata filter values 0 and 1 are constants, not code addresses.
    if (region.filter > 1 && region.filter != ExceptionRegion::Invalid) {
        add(tr("Go to filter"), region.filter);
    }
}

void ExceptionBar::setRegions(const QList<ExceptionRegion> &value, quint64 address)
{
    regions = value;
    navigator->menu()->clear();
    for (const auto &region : regions) {
        addRegion(navigator->menu(), region);
    }
    navigator->setText(tr("Exceptions (%1)").arg(regions.size()));
    setVisible(!regions.isEmpty());
    setAddress(address);
}

void ExceptionBar::setAddress(quint64 address)
{
    QStringList details;
    details << tr("Navigate this function's protected ranges and exception handlers (read-only)");
    for (const auto &region : regions) {
        if (region.contains(address) || region.handler == address) {
            details << region.description();
        }
    }
    navigator->setToolTip(details.join(QStringLiteral("\n\n"))
                              .toHtmlEscaped()
                              .replace(QLatin1Char('\n'), QStringLiteral("<br>")));
}

void ExceptionBar::populateMenu(QMenu *menu, quint64 address, quint64 size)
{
    menu->clear();
    for (const auto &region : regions) {
        if (region.touches(address, size)) {
            addRegion(menu, region);
        }
    }
    menu->menuAction()->setVisible(!menu->isEmpty());
}
