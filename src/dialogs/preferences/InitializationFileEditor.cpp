#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDialog>
#include <QLabel>
#include <QTextEdit>
#include <QTextStream>
#include <QUrl>

#include "InitializationFileEditor.h"
#include "ui_InitializationFileEditor.h"

#include "PreferencesDialog.h"

#include "common/Configuration.h"
#include "common/Helpers.h"

InitializationFileEditor::InitializationFileEditor(PreferencesDialog *dialog)
    : QDialog(dialog)
    , ui(new Ui::InitializationFileEditor)
{
    ui->setupUi(this);
    connect(ui->saveRC, &QDialogButtonBox::accepted, this, &InitializationFileEditor::saveIaitoRC);
    connect(
        ui->executeNow,
        &QDialogButtonBox::accepted,
        this,
        &InitializationFileEditor::executeIaitoRC);
    connect(
        ui->ConfigFileEdit, &QPlainTextEdit::modificationChanged, ui->saveRC, &QWidget::setEnabled);
    connect(
        ui->ConfigFileEdit2, &QPlainTextEdit::modificationChanged, ui->saveRC, &QWidget::setEnabled);

    const QDir iaitoRCDirectory = Core()->getIaitoRCDefaultDirectory();
    auto iaitoRCFileInfo = QFileInfo(iaitoRCDirectory, "rc");
    QString iaitorcPath = iaitoRCFileInfo.absoluteFilePath();

    ui->iaitoRCLoaded->setTextInteractionFlags(Qt::TextBrowserInteraction);
    ui->iaitoRCLoaded->setOpenExternalLinks(true);
    ui->iaitoRCLoaded->setText(
        tr("Script is loaded from <a href=\"%1\">%2</a>")
            .arg(
                QUrl::fromLocalFile(iaitoRCDirectory.absolutePath()).toString(),
                iaitorcPath.toHtmlEscaped()));

    ui->saveRC->setDisabled(true);
    ui->executeNow->button(QDialogButtonBox::Retry)->setText("Execute");
    ui->ConfigFileEdit->clear();
    if (iaitoRCFileInfo.exists()) {
        QFile f(iaitorcPath);
        if (f.open(QIODevice::ReadWrite | QIODevice::Text)) {
            ui->ConfigFileEdit->setPlainText(f.readAll());
        }
        f.close();
    }
    ui->ConfigFileEdit2->clear();
    auto rc2 = QFileInfo(iaitoRCDirectory, "rc2");
    if (rc2.exists()) {
        QString rc2file = rc2.absoluteFilePath();
        QFile f(rc2file);
        if (f.open(QIODevice::ReadWrite | QIODevice::Text)) {
            ui->ConfigFileEdit2->setPlainText(f.readAll());
        }
        f.close();
    }
}

InitializationFileEditor::~InitializationFileEditor(){};

void InitializationFileEditor::saveIaitoRC()
{
    const QDir iaitoRCDirectory = Core()->getIaitoRCDefaultDirectory();
    if (!iaitoRCDirectory.exists()) {
        iaitoRCDirectory.mkpath(".");
    }
    auto iaitoRCFileInfo = QFileInfo(iaitoRCDirectory, "rc");
    QString iaitorcPath = iaitoRCFileInfo.absoluteFilePath();

    QFile iaitoRC(iaitorcPath);
    if (iaitoRC.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&iaitoRC);
        QString text = ui->ConfigFileEdit->toPlainText();
        out << text;
        iaitoRC.close();
    }
    ui->ConfigFileEdit->document()->setModified(false);

    // save second thing
    auto rc2 = QFileInfo(iaitoRCDirectory, "rc2");
    QString rc2file = rc2.absoluteFilePath();

    QFile f(rc2file);
    if (f.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&f);
        QString text = ui->ConfigFileEdit2->toPlainText();
        out << text;
        f.close();
    }
    ui->ConfigFileEdit2->document()->setModified(false);
}

void InitializationFileEditor::executeIaitoRC()
{
    saveIaitoRC();
    Core()->loadDefaultIaitoRC();
    Core()->loadSecondaryIaitoRC();
}
