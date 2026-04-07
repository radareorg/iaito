#include "RegistersWidget.h"
#include "common/JsonModel.h"
#include "ui_RegistersWidget.h"

#include "core/MainWindow.h"

#include <QCollator>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolTip>

namespace {
static const int kMaxTooltipWidth = 400;
static const int kMaxTooltipDisasmPreviewLines = 10;
static const int kMaxTooltipHexdumpBytes = 64;
} // namespace

RegistersWidget::RegistersWidget(MainWindow *main)
    : IaitoDockWidget(main)
    , ui(new Ui::RegistersWidget)
    , addressContextMenu(this, main)
{
    ui->setupUi(this);

    // setup register layout
    registerLayout->setVerticalSpacing(2);
    registerLayout->setHorizontalSpacing(4);
    registerLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    ui->verticalLayout->addLayout(registerLayout);

    refreshDeferrer = createRefreshDeferrer([this]() { updateContents(); });

    connect(Core(), &IaitoCore::refreshAll, this, &RegistersWidget::updateContents);
    connect(Core(), &IaitoCore::registersChanged, this, &RegistersWidget::updateContents);
    connect(Config(), &Configuration::fontsUpdated, this, [this]() {
        QFont font = Config()->getSmallFont();
        for (int i = 0; i < registerLayout->count(); i++) {
            QWidget *w = registerLayout->itemAt(i)->widget();
            if (w) {
                w->setFont(font);
            }
        }
    });

    // Hide shortcuts because there is no way of selecting an item and trigger
    // them
    for (auto &action : addressContextMenu.actions()) {
        action->setShortcut(QKeySequence());
    }

    setTooltipStylesheet();
    connect(Config(), &Configuration::colorsUpdated, this, &RegistersWidget::setTooltipStylesheet);
}

RegistersWidget::~RegistersWidget() = default;

void RegistersWidget::updateContents()
{
    if (!refreshDeferrer->attemptRefresh(nullptr)) {
        return;
    }
    setRegisterGrid();
}

void RegistersWidget::setRegisterGrid()
{
    int i = 0;
    int col = 0;
    QLabel *registerLabel;
    QLineEdit *registerEditValue;
    const auto registerRefs = Core()->getRegisterRefValues();

    registerLen = registerRefs.size();
    for (auto &reg : registerRefs) {
        // check if we already filled this grid space with label/value
        if (!registerLayout->itemAtPosition(i, col)) {
            registerLabel = new QLabel;
            registerLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            registerLabel->setMaximumWidth(80);
            registerLabel->setFont(Config()->getSmallFont());
            registerLabel->setStyleSheet("font-weight: bold;");
            registerLabel->setCursor(Qt::PointingHandCursor);
            registerEditValue = new QLineEdit;
            registerEditValue->setMaximumWidth(200);
            registerEditValue->setFont(Config()->getSmallFont());
            registerLabel->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(
                registerLabel,
                &QWidget::customContextMenuRequested,
                this,
                [this, registerEditValue, registerLabel](QPoint p) {
                    openContextMenu(registerLabel->mapToGlobal(p), registerEditValue->text());
                });
            registerLabel->installEventFilter(this);
            labelToValue[registerLabel] = registerEditValue;
            registerEditValue->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(
                registerEditValue,
                &QWidget::customContextMenuRequested,
                this,
                [this, registerEditValue](QPoint p) {
                    openContextMenu(registerEditValue->mapToGlobal(p), registerEditValue->text());
                });
            // add label and register value to grid
            registerLayout->addWidget(registerLabel, i, col);
            registerLayout->addWidget(registerEditValue, i, col + 1);
            connect(registerEditValue, &QLineEdit::editingFinished, [=]() {
                QString regNameString = registerLabel->text();
                QString regValueString = registerEditValue->text();
                Core()->setRegister(regNameString, regValueString);
            });
        } else {
            QWidget *regNameWidget = registerLayout->itemAtPosition(i, col)->widget();
            QWidget *regValueWidget = registerLayout->itemAtPosition(i, col + 1)->widget();
            registerLabel = qobject_cast<QLabel *>(regNameWidget);
            registerEditValue = qobject_cast<QLineEdit *>(regValueWidget);
        }

        // Highlight changed registers only when we have previous values to compare
        bool valueChanged = previousValues.contains(reg.name)
                            && previousValues[reg.name] != reg.value;
        if (valueChanged) {
            registerEditValue->setStyleSheet(
                "QLineEdit { border: 1px solid green; background-color: rgba(0, 255, 0, 30); }");
        } else if (!registerEditValue->styleSheet().isEmpty()) {
            registerEditValue->setStyleSheet(QString());
        }

        // Only update text if it actually changed to avoid unnecessary repaints
        if (registerLabel->text() != reg.name) {
            registerLabel->setText(reg.name);
        }

        registerLabel->setToolTip(reg.ref);
        registerEditValue->setToolTip(reg.ref);

        if (registerEditValue->text() != reg.value) {
            registerEditValue->setPlaceholderText(reg.value);
            registerEditValue->setText(reg.value);
        }
        i++;
        // decide if we should change column
        if (i >= (registerLen + numCols - 1) / numCols) {
            i = 0;
            col += 2;
        }
    }

    // Update previous values cache for next refresh
    previousValues.clear();
    for (auto &reg : registerRefs) {
        previousValues[reg.name] = reg.value;
    }
}

void RegistersWidget::openContextMenu(QPoint point, QString address)
{
    addressContextMenu.setTarget(address.toULongLong(nullptr, 16));
    addressContextMenu.exec(point);
}

bool RegistersWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QLineEdit *valueWidget = labelToValue.value(obj, nullptr);
        if (valueWidget) {
            bool ok;
            RVA addr = valueWidget->text().toULongLong(&ok, 16);
            if (ok) {
                Core()->seekAndShow(addr);
                return true;
            }
        }
    } else if (event->type() == QEvent::ToolTip) {
        QLineEdit *valueWidget = labelToValue.value(obj, nullptr);
        if (valueWidget) {
            bool ok;
            RVA addr = valueWidget->text().toULongLong(&ok, 16);
            if (ok) {
                QString tip = buildRichTooltip(addr);
                if (!tip.isEmpty()) {
                    QHelpEvent *he = static_cast<QHelpEvent *>(event);
                    QToolTip::showText(he->globalPos(), tip, qobject_cast<QWidget *>(obj));
                    return true;
                }
            }
        }
    }
    return IaitoDockWidget::eventFilter(obj, event);
}

QString RegistersWidget::buildRichTooltip(RVA address)
{
    const QFont &fnt = Config()->getFont();

    // Map/section info
    QString mapName = Core()->cmd(QStringLiteral("dm.@ %1").arg(address)).trimmed();

    // Disassembly preview
    QStringList disasmPreview
        = Core()->getDisassemblyPreview(address, kMaxTooltipDisasmPreviewLines);

    // Hexdump preview
    QString hexPreview = Core()->getHexdumpPreview(address, kMaxTooltipHexdumpBytes);

    if (mapName.isEmpty() && disasmPreview.isEmpty() && hexPreview.isEmpty())
        return QString();

    QString tip = QStringLiteral(
                      "<html><div style=\"font-family: %1; font-size: %2pt; "
                      "white-space: nowrap;\">")
                      .arg(fnt.family())
                      .arg(qMax(6, fnt.pointSize() - 1));

    if (!mapName.isEmpty()) {
        tip += QStringLiteral("<div style=\"margin-bottom: 6px;\"><strong>Map</strong>: %1</div>")
                   .arg(mapName.toHtmlEscaped());
    }

    if (!disasmPreview.isEmpty()) {
        tip += QStringLiteral(
                   "<div style=\"margin-bottom: 6px;\"><strong>Disassembly</strong>"
                   ":<br>%1</div>")
                   .arg(disasmPreview.join("<br>"));
    }

    if (!hexPreview.isEmpty()) {
        tip += QStringLiteral("<div><strong>Hexdump</strong>:<br>%1</div>").arg(hexPreview);
    }

    tip += "</div></html>";
    return tip;
}

void RegistersWidget::setTooltipStylesheet()
{
    setStyleSheet(QStringLiteral(
                      "QToolTip { border-width: 1px; max-width: %1px;"
                      "opacity: 230; background-color: %2;"
                      "color: %3; border-color: %3;}")
                      .arg(kMaxTooltipWidth)
                      .arg(Config()->getColor("gui.tooltip.background").name())
                      .arg(Config()->getColor("gui.tooltip.foreground").name()));
}
