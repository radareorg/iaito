#ifndef IAITO_TEXTEDIT_DIALOG_H
#define IAITO_TEXTEDIT_DIALOG_H

#include <QAction>
#include <QDialog>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include "core/IaitoCommon.h"

class TextEditDialog;

class TextEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit TextEditDialog(const QString& initialText, QWidget *parent = nullptr);
    ~TextEditDialog();
    QString getEditedText() const;

protected:
    void accept() override;

private:
    QTextEdit *textEdit;
    QString editedText;
};

IAITO_EXPORT QString openTextEditDialog(const QString& initialText, QWidget *parent = nullptr);
IAITO_EXPORT bool openTextEditDialogFromFile(const QString& textFileName, QWidget *parent = nullptr);

#endif // HELPERS_H
