#include <QLabel>
#include <QFontDialog>
#include <QTextEdit>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDialogButtonBox>
#include <QUrl>

#include "InitializationFileEditor.h"
#include "ui_InitializationFileEditor.h"

#include "PreferencesDialog.h"

#include "common/Helpers.h"
#include "common/Configuration.h"

InitializationFileEditor::InitializationFileEditor(PreferencesDialog *dialog)
    : QDialog(dialog),
      ui(new Ui::InitializationFileEditor)
{
    ui->setupUi(this);
    connect(ui->saveRC, &QDialogButtonBox::accepted, this, &InitializationFileEditor::saveIaitoRC);
    connect(ui->executeNow, &QDialogButtonBox::accepted, this, &InitializationFileEditor::executeIaitoRC);
    connect(ui->ConfigFileEdit, &QPlainTextEdit::modificationChanged, ui->saveRC, &QWidget::setEnabled);

    const QDir iaitoRCDirectory = Core()->getIaitoRCDefaultDirectory();
    auto iaitoRCFileInfo = QFileInfo(iaitoRCDirectory, "rc");
    QString iaitoRCLocation = iaitoRCFileInfo.absoluteFilePath();
    
    ui->iaitoRCLoaded->setTextInteractionFlags(Qt::TextBrowserInteraction);
    ui->iaitoRCLoaded->setOpenExternalLinks(true);
    ui->iaitoRCLoaded->setText(tr("Script is loaded from <a href=\"%1\">%2</a>")
                                .arg(QUrl::fromLocalFile(iaitoRCDirectory.absolutePath()).toString(), iaitoRCLocation.toHtmlEscaped()));
    
    ui->executeNow->button(QDialogButtonBox::Retry)->setText("Execute");
    ui->ConfigFileEdit->clear();
    if(iaitoRCFileInfo.exists()){
        QFile iaitoRC(iaitoRCLocation);
        if(iaitoRC.open(QIODevice::ReadWrite | QIODevice::Text)){ 
            ui->ConfigFileEdit->setPlainText(iaitoRC.readAll()); 
        }
        iaitoRC.close();
    }
    ui->saveRC->setDisabled(true);
}

InitializationFileEditor::~InitializationFileEditor() {};


void InitializationFileEditor::saveIaitoRC(){
    const QDir iaitoRCDirectory = Core()->getIaitoRCDefaultDirectory();
    if(!iaitoRCDirectory.exists()){
        iaitoRCDirectory.mkpath(".");
    }
    auto iaitoRCFileInfo = QFileInfo(iaitoRCDirectory, "rc");
    QString iaitoRCLocation = iaitoRCFileInfo.absoluteFilePath();
   
    QFile iaitoRC(iaitoRCLocation);
    if(iaitoRC.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)){ 
        QTextStream out(&iaitoRC);
        QString text = ui->ConfigFileEdit->toPlainText();
        out << text;
        iaitoRC.close();
    }
    ui->ConfigFileEdit->document()->setModified(false);
}

void InitializationFileEditor::executeIaitoRC(){
    saveIaitoRC();
    Core()->loadDefaultIaitoRC();
}
