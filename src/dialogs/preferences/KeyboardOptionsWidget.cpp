#include "KeyboardOptionsWidget.h"
#include "PreferencesDialog.h"
#include "common/Configuration.h"
#include "ui_KeyboardOptionsWidget.h"

#include <QLineEdit>
#include <QString>

KeyboardOptionsWidget::KeyboardOptionsWidget(PreferencesDialog *dialog)
    : QDialog(dialog)
    , ui(new Ui::KeyboardOptionsWidget)
{
    setAttribute(Qt::WA_DeleteOnClose);
    ui->setupUi(this);
    // Load existing key.fX configurations and connect signals
    for (int i = 1; i <= 12; ++i) {
        const QString key = QStringLiteral("key.f%1").arg(i);
        QLineEdit *le = findChild<QLineEdit *>(QStringLiteral("f%1LineEdit").arg(i));
        if (!le) {
            continue;
        }
        // Initialize without triggering signal
        le->blockSignals(true);
        le->setText(Core()->getConfig(key));
        le->blockSignals(false);
        // Update config when edited
        connect(le, &QLineEdit::textChanged, this, [key](const QString &text) {
            Config()->setConfig(key, text);
        });
    }
}

KeyboardOptionsWidget::~KeyboardOptionsWidget() {}