#include "widgets/CalculatorWidget.h"

#include "common/Helpers.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

CalculatorWidget::CalculatorWidget(MainWindow *main)
    : IaitoDockWidget(main)
{
    setObjectName(
        main ? main->getUniqueObjectName(QStringLiteral("CalculatorWidget"))
             : QStringLiteral("CalculatorWidget"));
    setWindowTitle(tr("Calculator"));
    toggleViewAction()->setText(tr("Calculator"));

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);

    auto *inputLayout = new QHBoxLayout;
    inputLineEdit = new QLineEdit(container);
    inputLineEdit->setPlaceholderText(tr("rax2 expression"));
    auto *runButton = new QPushButton(tr("Run"), container);
    auto *clearButton = new QPushButton(tr("Clear"), container);
    inputLayout->addWidget(inputLineEdit, 1);
    inputLayout->addWidget(runButton);
    inputLayout->addWidget(clearButton);

    previewLabel = new QLabel(container);
    previewLabel->setMinimumHeight(previewLabel->fontMetrics().height());

    outputTextEdit = new QPlainTextEdit(container);
    outputTextEdit->setReadOnly(true);
    outputTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap);

    qhelpers::bindFont(inputLineEdit, false, true);
    qhelpers::bindFont(previewLabel, false, true);
    qhelpers::bindFont(outputTextEdit, false, true);

    layout->addLayout(inputLayout);
    layout->addWidget(previewLabel);
    layout->addWidget(outputTextEdit, 1);
    setWidget(container);

    connect(inputLineEdit, &QLineEdit::textEdited, this, &CalculatorWidget::updatePreview);
    connect(inputLineEdit, &QLineEdit::returnPressed, this, &CalculatorWidget::runExpression);
    connect(runButton, &QPushButton::clicked, this, &CalculatorWidget::runExpression);
    connect(clearButton, &QPushButton::clicked, this, &CalculatorWidget::clearOutput);
}

QString CalculatorWidget::escapedExpression(QString expression)
{
    expression.replace(QLatin1Char('"'), QStringLiteral(" # "));
    return expression;
}

QWidget *CalculatorWidget::widgetToFocusOnRaise()
{
    return inputLineEdit;
}

void CalculatorWidget::updatePreview()
{
    const QString input = inputLineEdit->text().trimmed();
    if (input.isEmpty()) {
        previewLabel->clear();
        return;
    }

    const QString result = Core()->cmd(QStringLiteral("\"?v %1\"").arg(escapedExpression(input)));
    previewLabel->setText(result.trimmed());
}

void CalculatorWidget::runExpression()
{
    const QString input = inputLineEdit->text().trimmed();
    if (input.isEmpty()) {
        return;
    }
    if (input == QLatin1String("cls") || input == QLatin1String("clear")) {
        clearOutput();
        inputLineEdit->clear();
        previewLabel->clear();
        return;
    }

    const QString result = Core()->cmd(QStringLiteral("\"? %1\"").arg(escapedExpression(input)));
    const QString previous = outputTextEdit->toPlainText();
    outputTextEdit->setPlainText(
        QStringLiteral("> %1\n%2\n\n%3").arg(input, result.trimmed(), previous));
    inputLineEdit->clear();
    previewLabel->clear();
}

void CalculatorWidget::clearOutput()
{
    outputTextEdit->clear();
}
