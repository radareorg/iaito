#include "common/TextEditDialog.h"

TextEditDialog::~TextEditDialog() {
}

TextEditDialog::TextEditDialog(const QString& initialText, QWidget *parent)
        : QDialog(parent), editedText(initialText)
{
    setWindowTitle("Edit Text");

    textEdit = new QTextEdit(this);
    textEdit->setPlainText(initialText);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(textEdit);
    layout->addWidget(buttonBox);

    connect(buttonBox->button(QDialogButtonBox::Save), &QPushButton::clicked, this, &TextEditDialog::accept);
    connect(buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this, &TextEditDialog::reject);
}

QString TextEditDialog::getEditedText() const {
        return editedText;
}

void TextEditDialog::accept()
{
    editedText = textEdit->toPlainText();
    QDialog::accept();
}

IAITO_EXPORT QString openTextEditDialog(const QString& initialText, QWidget *parent) {
    TextEditDialog dialog(initialText, parent);
    if (dialog.exec() == QDialog::Accepted) {
        return dialog.getEditedText();
    }
    return initialText; // Return original text if canceled
}

IAITO_EXPORT bool openTextEditDialogFromFile(const QString& textFileName, QWidget *parent) {
    const char *dp = textFileName.toUtf8().constData();
    char *data = r_file_slurp (dp, NULL);
    if (data == NULL) {
        return false;
    }
    const QString qdata (data);
    QString s = openTextEditDialog(qdata, parent);
    ut8 *newData = (ut8*)r_str_newf ("%s\n", s.toUtf8().constData());
    bool res = true;
    if (!r_file_dump (dp, newData, -1, false)) {
	res = false;
    }
    free (newData);
    free (data);
    return res;
}
