#include "ResponsiveDialog.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QInputMethod>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollArea>
#include <QScroller>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

namespace {

bool mobilePlatform()
{
    return QGuiApplication::platformName() == QLatin1String("android")
           || QGuiApplication::platformName() == QLatin1String("ios")
           || qEnvironmentVariableIntValue("IAITO_MOBILE_UI") == 1;
}

// Qt's standard dialogs manage their own layouts (and may use a native window).
// Do not replace layouts which Qt itself rebuilds in showEvent().
bool hasPlatformLayout(QDialog *dialog)
{
    return qobject_cast<QFileDialog *>(dialog) || qobject_cast<QMessageBox *>(dialog)
           || qobject_cast<QInputDialog *>(dialog) || qobject_cast<QProgressDialog *>(dialog);
}

QWidget *dialogActions(QDialog *dialog)
{
    for (auto *box : dialog->findChildren<QDialogButtonBox *>()) {
        bool inPage = false;
        for (QWidget *p = box->parentWidget(); p && p != dialog; p = p->parentWidget()) {
            if (qobject_cast<QTabWidget *>(p) || qobject_cast<QStackedWidget *>(p)
                || qobject_cast<QDialog *>(p)) {
                inPage = true;
                break;
            }
        }
        if (!inPage) {
            return box;
        }
    }
    return nullptr;
}

void activateChildLayouts(QWidget *widget)
{
    if (widget->layout()) {
        widget->layout()->activate();
    }
    for (auto *child : widget->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!child->isWindow()) {
            activateChildLayouts(child);
        }
    }
}

class DialogAdapter : public QObject
{
public:
    explicit DialogAdapter(QDialog *dialog)
        : QObject(dialog)
        , dialog(dialog)
        , originalMaximum(dialog->maximumSize())
    {
        setObjectName(QStringLiteral("iaitoDialogAdapter"));
        dialog->installEventFilter(this);
        if (mobilePlatform() && !dialog->property("iaitoMobilePage").toBool()) {
            // Reserve space outside the body layout so the close control stays
            // reachable even when the contents need scrolling or Qt rebuilds them.
            header = new QWidget(dialog);
            header->setObjectName(QStringLiteral("iaitoDialogHeader"));
            header->setAutoFillBackground(true);
            header->setBackgroundRole(QPalette::Button);
            auto *layout = new QHBoxLayout(header);
            layout->setContentsMargins(12, 0, 2, 0);
            auto *title = new QLabel(dialog->windowTitle(), header);
            title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            QFont font = title->font();
            font.setBold(true);
            title->setFont(font);
            layout->addWidget(title, 1);
            connect(dialog, &QWidget::windowTitleChanged, title, &QLabel::setText);
            auto *close = new QToolButton(header);
            close->setObjectName(QStringLiteral("iaitoDialogCloseButton"));
            close->setText(QString::fromUtf8("×"));
            close->setAccessibleName(tr("Close dialog"));
            close->setToolTip(tr("Close"));
            close->setMinimumSize(44, 44);
            layout->addWidget(close);
            connect(close, &QToolButton::clicked, dialog, &QWidget::close);
            const QMargins margins = dialog->contentsMargins();
            dialog->setContentsMargins(
                margins.left() + 2, margins.top() + 48, margins.right() + 2, margins.bottom() + 2);
        }
        // Qt's iOS input context already pans the root view to reveal the cursor.
        // Moving/resizing a dialog in response changes the keyboard's window-relative
        // rectangle again, feeding back into that panning and making inputs jump.
        if (QGuiApplication::platformName() != QLatin1String("ios")) {
            connect(
                qApp->inputMethod(),
                &QInputMethod::keyboardRectangleChanged,
                this,
                &DialogAdapter::scheduleFit);
            connect(
                qApp->inputMethod(),
                &QInputMethod::visibleChanged,
                this,
                &DialogAdapter::scheduleFit);
        }
    }

    void fit()
    {
        if (updating || !dialog->isWindow() || !dialog->layout()) {
            return;
        }
        QScopedValueRollback<bool> guard(updating, true);
        auto *window = dialog->windowHandle();
        auto *screen = window ? window->screen() : QGuiApplication::primaryScreen();
        if (!screen) {
            return;
        }
        if (!screenConnection && window) {
            screenConnection = connect(window, &QWindow::screenChanged, this, [this]() {
                disconnect(geometryConnection);
                geometryConnection = {};
                scheduleFit();
            });
        }
        if (!geometryConnection) {
            geometryConnection = connect(
                screen, &QScreen::availableGeometryChanged, this, &DialogAdapter::scheduleFit);
        }
        QRect available = screen->availableGeometry();
        auto *input = qApp->inputMethod();
        const bool platformGeometry = dialog->isMaximized() || dialog->isFullScreen();
        if (QGuiApplication::platformName() != QLatin1String("ios") && input->isVisible()
            && QGuiApplication::focusWindow() == window && !platformGeometry) {
            const QRect keyboard = input->keyboardRectangle().toAlignedRect().translated(
                window->position());
            // Floating keyboards should not shrink the entire dialog.
            if (keyboard.width() >= available.width() / 2
                && keyboard.bottom() >= available.bottom()) {
                available.setBottom(qMin(available.bottom(), keyboard.top() - 1));
            }
        }
        const QMargins frame = window ? window->frameMargins() : QMargins();
        const QSize decorations(frame.left() + frame.right(), frame.top() + frame.bottom());
        // Maximized mobile windows follow the actual scene bounds (including
        // Split View/Stage Manager), which can be smaller than the screen.
        const QSize maximum = platformGeometry ? dialog->size() : available.size() - decorations;
        if (maximum.isEmpty()) {
            return;
        }
        QSize target = dialog->size().boundedTo(maximum);
        if (maximum.width() < 600) {
            target.setWidth(maximum.width());
        }
        if (!hasPlatformLayout(dialog)) {
            reflow(target.width());
        }
        // Reflow can change nested tab pages and their fixed action rows. Settle
        // those layouts before deciding whether the dialog needs an outer scroll area.
        activateChildLayouts(dialog);
        const QSize minimum = dialog->minimumSize().expandedTo(dialog->minimumSizeHint());
        if (!hasPlatformLayout(dialog)
            && (minimum.width() > target.width() || minimum.height() > target.height())) {
            ResponsiveDialog::makeScrollable(dialog, dialogActions(dialog));
            originalMaximum = QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
        if (!platformGeometry) {
            dialog->setMaximumSize(originalMaximum.boundedTo(maximum));
            dialog->resize(target);
            const QSize outer = dialog->size() + decorations;
            const QPoint position = available.topLeft()
                                    + QPoint(
                                        (available.width() - outer.width()) / 2,
                                        (available.height() - outer.height()) / 2);
            if (dialog->pos() != position) {
                dialog->move(position);
            }
        }
        if (header) {
            header->setGeometry(2, 2, dialog->width() - 4, 44);
            header->show();
            header->raise();
        }
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (header && event->type() == QEvent::Resize) {
            header->setGeometry(2, 2, dialog->width() - 4, 44);
        }
        if (!updating && dialog->isVisible()) {
            switch (event->type()) {
            case QEvent::Resize:
            case QEvent::LayoutRequest:
            case QEvent::FontChange:
            case QEvent::StyleChange:
                scheduleFit();
                break;
            default:
                break;
            }
        }
        return false;
    }

private:
    void scheduleFit()
    {
        if (pending || updating) {
            return;
        }
        pending = true;
        QTimer::singleShot(0, this, [this]() {
            pending = false;
            if (dialog->isVisible()) {
                fit();
            }
        });
    }

    void reflow(int width)
    {
        // Reserve both page margins and the body's scrollbar before wrapping rows.
        const int contentWidth
            = qMax(1, width - 72 - dialog->style()->pixelMetric(QStyle::PM_ScrollBarExtent));
        for (auto *button : dialog->findChildren<QAbstractButton *>()) {
            if (button->window() != dialog) {
                continue;
            }
            if (!qobject_cast<QCheckBox *>(button) && !qobject_cast<QRadioButton *>(button)) {
                continue;
            }
            const char *original = "iaitoUnwrappedText";
            const char *wrapped = "iaitoWrappedText";
            if (!button->property(original).isValid()
                || button->text() != button->property(wrapped).toString()) {
                button->setProperty(original, button->text());
            }
            QString text = button->property(original).toString();
            if (width < 600) {
                QTextLayout layout(text, button->font());
                QTextOption option;
                option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
                layout.setTextOption(option);
                QStringList lines;
                layout.beginLayout();
                while (true) {
                    auto line = layout.createLine();
                    if (!line.isValid()) {
                        break;
                    }
                    line.setLineWidth(qMax(1, contentWidth - 56));
                    lines << text.mid(line.textStart(), line.textLength()).trimmed();
                }
                layout.endLayout();
                text = lines.join('\n');
            }
            button->setProperty(wrapped, text);
            button->setText(text);
        }
        if (width < 600) {
            for (auto *label : dialog->findChildren<QLabel *>()) {
                if (label->window() == dialog) {
                    label->setWordWrap(true);
                }
            }
            for (auto *tabs : dialog->findChildren<QTabWidget *>()) {
                if (tabs->window() != dialog) {
                    continue;
                }
                tabs->setUsesScrollButtons(true);
                tabs->setElideMode(Qt::ElideRight);
            }
        }
        for (auto *form : dialog->findChildren<QFormLayout *>()) {
            if (form->parentWidget()->window() != dialog) {
                continue;
            }
            const char *key = "iaitoOriginalRowWrapPolicy";
            if (!form->property(key).isValid()) {
                form->setProperty(key, int(form->rowWrapPolicy()));
            }
            form->setRowWrapPolicy(
                width < 600 ? QFormLayout::WrapLongRows
                            : QFormLayout::RowWrapPolicy(form->property(key).toInt()));
        }
        for (auto *combo : dialog->findChildren<QComboBox *>()) {
            if (combo->window() != dialog) {
                continue;
            }
            const char *key = "iaitoOriginalSizeAdjustPolicy";
            const char *lengthKey = "iaitoOriginalMinimumContentsLength";
            const char *widthKey = "iaitoOriginalMinimumWidth";
            if (!combo->property(key).isValid()) {
                combo->setProperty(key, int(combo->sizeAdjustPolicy()));
                combo->setProperty(lengthKey, combo->minimumContentsLength());
                combo->setProperty(widthKey, combo->minimumWidth());
            }
            int length = combo->property(lengthKey).toInt();
            if (width < 600) {
                // Retain readable text when a form wraps. A zero contents length
                // leaves only the arrow and also makes Qt's list popup too narrow.
                for (int i = 0; i < combo->count(); ++i) {
                    length = qMax(length, int(combo->itemText(i).size()));
                }
                length = qBound(1, length, 20);
            }
            combo->setMinimumContentsLength(length);
            combo->setSizeAdjustPolicy(
                width < 600 ? QComboBox::AdjustToMinimumContentsLengthWithIcon
                            : QComboBox::SizeAdjustPolicy(combo->property(key).toInt()));
            while (width < 600 && length > 1 && combo->sizeHint().width() > contentWidth) {
                combo->setMinimumContentsLength(--length);
            }
            // Qt can retain its old minimumSizeHint after changing the contents
            // length. An explicit minimum keeps that cached width out of the form.
            combo->setMinimumWidth(
                width < 600 ? qMin(contentWidth, combo->sizeHint().width())
                            : combo->property(widthKey).toInt());
        }
        for (auto *layout : dialog->findChildren<QBoxLayout *>()) {
            // Combo popup windows are QObject descendants too. Qt owns their
            // layout and repositions them while opening; reflowing it here can
            // send another layout request to the dialog and move the popup again.
            if (layout->parentWidget()->window() != dialog
                || qobject_cast<QDialogButtonBox *>(layout->parentWidget())) {
                continue;
            }
            const char *key = "iaitoOriginalDirection";
            if (!layout->property(key).isValid()) {
                layout->setProperty(key, int(layout->direction()));
            }
            auto direction = QBoxLayout::Direction(layout->property(key).toInt());
            if (direction != QBoxLayout::LeftToRight && direction != QBoxLayout::RightToLeft) {
                continue;
            }
            int minimumWidth = 0;
            for (int i = 0; i < layout->count(); ++i) {
                minimumWidth += layout->itemAt(i)->minimumSize().width();
            }
            minimumWidth += qMax(0, layout->count() - 1) * qMax(0, layout->spacing());
            layout->setDirection(minimumWidth > contentWidth ? QBoxLayout::TopToBottom : direction);
        }
        for (auto *splitter : dialog->findChildren<QSplitter *>()) {
            if (splitter->window() != dialog) {
                continue;
            }
            const char *key = "iaitoOriginalOrientation";
            if (!splitter->property(key).isValid()) {
                splitter->setProperty(key, int(splitter->orientation()));
            }
            auto orientation = Qt::Orientation(splitter->property(key).toInt());
            splitter->setOrientation(width < 600 ? Qt::Vertical : orientation);
        }
        for (auto *box : dialog->findChildren<QDialogButtonBox *>()) {
            if (box->window() != dialog) {
                continue;
            }
            int buttonsWidth = 0;
            for (auto *button : box->buttons()) {
                if (!button->isHidden()) {
                    buttonsWidth += button->sizeHint().width() + 12;
                }
            }
            box->setOrientation(buttonsWidth > contentWidth ? Qt::Vertical : Qt::Horizontal);
        }
    }

    QDialog *dialog;
    QWidget *header = nullptr;
    QSize originalMaximum;
    QMetaObject::Connection screenConnection;
    QMetaObject::Connection geometryConnection;
    bool updating = false;
    bool pending = false;
};

void fitDialog(QDialog *dialog)
{
    if (!dialog->isWindow()) {
        return;
    }
    // Native mobile pickers manage presentation and keyboard avoidance themselves.
    // Adding our header or changing their geometry interferes with that lifecycle.
    auto *files = qobject_cast<QFileDialog *>(dialog);
    if (mobilePlatform() && files && !files->testOption(QFileDialog::DontUseNativeDialog)) {
        return;
    }
    auto *adapter = dialog->findChild<QObject *>(
        QStringLiteral("iaitoDialogAdapter"), Qt::FindDirectChildrenOnly);
    if (!adapter) {
        adapter = new DialogAdapter(dialog);
    }
    static_cast<DialogAdapter *>(adapter)->fit();
}

class DialogApplicationFilter : public QObject
{
public:
    explicit DialogApplicationFilter(QObject *parent)
        : QObject(parent)
    {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show) {
            if (auto *dialog = qobject_cast<QDialog *>(watched)) {
                fitDialog(dialog);
            }
        }
        return false;
    }
};

} // namespace

ResponsiveDialog::ResponsiveDialog(QWidget *parent)
    : QDialog(parent)
{}

void ResponsiveDialog::installApplicationHandler()
{
    const QString name = QStringLiteral("iaitoDialogApplicationFilter");
    if (!qApp->findChild<QObject *>(name, Qt::FindDirectChildrenOnly)) {
        auto *filter = new DialogApplicationFilter(qApp);
        filter->setObjectName(name);
        qApp->installEventFilter(filter);
    }
}

bool ResponsiveDialog::prepareMobilePage(QDialog *dialog)
{
    if (!mobilePlatform()) {
        return false;
    }
    dialog->setProperty("iaitoMobilePage", true);
    dialog->setWindowFlags(Qt::Window);
    dialog->setAutoFillBackground(true);
    if (dialog->layout()) {
        dialog->layout()->setSizeConstraint(QLayout::SetNoConstraint);
    }
    dialog->setMinimumSize(0, 0);
    dialog->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    // Maximized, rather than fullscreen, keeps iOS's status bar and home-indicator
    // safe areas and follows changes to the tablet's resizable scene.
    dialog->setWindowState(Qt::WindowMaximized);
    return true;
}

QScrollArea *ResponsiveDialog::makeScrollable(QWidget *page, QWidget *actions)
{
    const QString name = QStringLiteral("responsiveScrollArea");
    if (auto *scroll = page->findChild<QScrollArea *>(name, Qt::FindDirectChildrenOnly)) {
        return scroll;
    }
    auto *bodyLayout = page->layout();
    if (!bodyLayout) {
        return nullptr;
    }
    const auto margins = bodyLayout->contentsMargins();
    auto *body = new QWidget;
    body->setLayout(bodyLayout);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(margins);
    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(name);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(body);
    layout->addWidget(scroll, 1);
    if (actions) {
        layout->addWidget(actions);
    }
    page->setMinimumSize(0, 0);
    page->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    connect(qApp, &QApplication::focusChanged, scroll, [scroll](QWidget *, QWidget *focus) {
        if (focus && scroll->widget() && scroll->widget()->isAncestorOf(focus)) {
            scroll->ensureWidgetVisible(focus);
        }
    });
    return scroll;
}

void ResponsiveDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    fitDialog(this);
}
