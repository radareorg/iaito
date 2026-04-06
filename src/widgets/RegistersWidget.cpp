#include "RegistersWidget.h"
#include "common/JsonModel.h"
#include "ui_RegistersWidget.h"

#include "core/MainWindow.h"

#include <QCollator>
#include <QLabel>
#include <QLineEdit>

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

    // Hide shortcuts because there is no way of selecting an item and trigger
    // them
    for (auto &action : addressContextMenu.actions()) {
        action->setShortcut(QKeySequence());
    }
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
            registerLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            registerLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            registerLabel->setMaximumWidth(80);
            registerLabel->setFont(Config()->getFont());
            registerLabel->setStyleSheet("font-weight: bold;");
            registerEditValue = new QLineEdit;
            registerEditValue->setMaximumWidth(200);
            registerEditValue->setFont(Config()->getFont());
            registerLabel->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(
                registerLabel,
                &QWidget::customContextMenuRequested,
                this,
                [this, registerEditValue, registerLabel](QPoint p) {
                    openContextMenu(registerLabel->mapToGlobal(p), registerEditValue->text());
                });
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
