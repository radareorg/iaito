#ifndef CALCULATORWIDGET_H
#define CALCULATORWIDGET_H

#include "widgets/IaitoDockWidget.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;

class CalculatorWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit CalculatorWidget(MainWindow *main);

private slots:
    void updatePreview();
    void runExpression();
    void clearOutput();

private:
    QWidget *widgetToFocusOnRaise() override;
    static QString escapedExpression(QString expression);

    QLineEdit *inputLineEdit = nullptr;
    QLabel *previewLabel = nullptr;
    QPlainTextEdit *outputTextEdit = nullptr;
};

#endif // CALCULATORWIDGET_H
