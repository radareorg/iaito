#include "IaitoDockWidget.h"
#include "core/MainWindow.h"

#include <QEvent>
// #include <QtWidgets/QShortcut>

IaitoDockWidget::IaitoDockWidget(MainWindow *parent, QAction *)
    : IaitoDockWidget(parent)
{}

IaitoDockWidget::IaitoDockWidget(MainWindow *parent)
    : QDockWidget(parent)
    , mainWindow(parent)
{
    // Install event filter to catch redraw widgets when needed
    installEventFilter(this);
    updateIsVisibleToUser();
    connect(toggleViewAction(), &QAction::triggered, this, &QWidget::raise);
}

IaitoDockWidget::~IaitoDockWidget() = default;

bool IaitoDockWidget::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::ZOrderChange
        || event->type() == QEvent::Paint || event->type() == QEvent::Close
        || event->type() == QEvent::Show || event->type() == QEvent::Hide) {
        updateIsVisibleToUser();
    }
    return QDockWidget::eventFilter(object, event);
}

QVariantMap IaitoDockWidget::serializeViewProprties()
{
    return {};
}

void IaitoDockWidget::deserializeViewProperties(const QVariantMap &) {}

void IaitoDockWidget::ignoreVisibilityStatus(bool ignore)
{
    this->ignoreVisibility = ignore;
    updateIsVisibleToUser();
}

void IaitoDockWidget::raiseMemoryWidget()
{
    show();
    raise();
    widgetToFocusOnRaise()->setFocus(Qt::FocusReason::TabFocusReason);
}

void IaitoDockWidget::toggleDockWidget(bool show)
{
    if (!show) {
        this->hide();
    } else {
        this->show();
        this->raise();
    }
}

QWidget *IaitoDockWidget::widgetToFocusOnRaise()
{
    return this;
}

void IaitoDockWidget::updateIsVisibleToUser()
{
    // Check if the user can actually see the widget.
    bool visibleToUser = isVisible() && !visibleRegion().isEmpty() && !ignoreVisibility;
    if (visibleToUser == isVisibleToUserCurrent) {
        return;
    }
    isVisibleToUserCurrent = visibleToUser;
    if (isVisibleToUserCurrent) {
        emit becameVisibleToUser();
    }
}

void IaitoDockWidget::closeEvent(QCloseEvent *event)
{
    QDockWidget::closeEvent(event);
    if (isTransient) {
        if (mainWindow) {
            mainWindow->removeWidget(this);
        }

        // remove parent, otherwise dock layout may still decide to use this
        // widget which is about to be deleted
        setParent(nullptr);

        deleteLater();
    }

    emit closed();
}

QString IaitoDockWidget::getDockNumber()
{
    auto name = this->objectName();
    if (name.contains(';')) {
        auto parts = name.split(';');
        if (parts.size() >= 2) {
            return parts[1];
        }
    }
    return QString();
}
