#include "MobilePanelDialog.h"
#include "core/DockManager.h"
#include "core/MainWindow.h"
#include "widgets/IaitoDockWidget.h"

#include <QApplication>
#include <QInputMethod>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QResizeEvent>
#include <QScroller>
#include <QVBoxLayout>

namespace {
class PanelList : public QListWidget
{
public:
    explicit PanelList(QWidget *parent)
        : QListWidget(parent)
    {
        setObjectName(QStringLiteral("mobilePanelList"));
        setFlow(QListView::LeftToRight);
        setWrapping(true);
        setResizeMode(QListView::Adjust);
        setMovement(QListView::Static);
        setSpacing(0);
        setIconSize(QSize(24, 24));
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setMinimumSize(0, 0);
        QScroller::grabGesture(viewport(), QScroller::TouchGesture);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QListWidget::resizeEvent(event);
        const int columns = qBound(1, viewport()->width() / 190, 4);
        const QSize cell(viewport()->width() / columns, qMax(52, fontMetrics().height() + 24));
        setGridSize(cell);
        for (int i = 0; i < count(); ++i) {
            item(i)->setSizeHint(cell);
        }
    }
};
} // namespace

MobilePanelDialog::MobilePanelDialog(MainWindow *main)
    : ResponsiveDialog(main)
{
    setObjectName(QStringLiteral("mobilePanelDialog"));
    setWindowTitle(tr("Panels"));
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(true);
    connect(this, &QDialog::finished, this, []() { qApp->inputMethod()->hide(); });
    auto *layout = new QVBoxLayout(this);
    auto *filter = new QLineEdit(this);
    filter->setObjectName(QStringLiteral("mobilePanelFilter"));
    filter->setPlaceholderText(tr("Find a panel…"));
    filter->setAccessibleName(tr("Find a panel"));
    filter->setClearButtonEnabled(true);
    filter->setMinimumHeight(44);
    layout->addWidget(filter);
    auto *list = new PanelList(this);
    layout->addWidget(list, 1);
    auto *empty = new QLabel(tr("No matching panels"), this);
    empty->setAlignment(Qt::AlignCenter);
    empty->hide();
    layout->addWidget(empty);
    QHash<QListWidgetItem *, QPointer<IaitoDockWidget>> panels;
    for (auto *dock : main->findChild<DockManager *>()->docks()) {
        const QString title = QString(dock->windowTitle()).remove(QLatin1Char('&'));
        auto *item = new QListWidgetItem(dock->windowIcon(), title, list);
        item->setToolTip(title);
        item->setData(Qt::UserRole, dock->objectName());
        panels.insert(item, dock);
    }
    list->sortItems();
    connect(filter, &QLineEdit::textChanged, this, [list, empty](const QString &text) {
        int matches = 0;
        for (int i = 0; i < list->count(); ++i) {
            auto *item = list->item(i);
            const bool match = item->text().contains(text.trimmed(), Qt::CaseInsensitive);
            item->setHidden(!match);
            matches += match;
        }
        empty->setVisible(matches == 0);
        list->scrollToTop();
    });
    auto activate = [this, panels](QListWidgetItem *item) {
        auto dock = panels.value(item);
        if (dock) {
            accept();
            dock->raiseMemoryWidget();
        }
    };
    connect(list, &QListWidget::itemClicked, this, activate);
    connect(list, &QListWidget::itemActivated, this, activate);
    resize(main->size().boundedTo(QSize(820, 640)) - QSize(32, 32));
    // Opening the panel picker should not summon the software keyboard.
    list->setFocus();
}
