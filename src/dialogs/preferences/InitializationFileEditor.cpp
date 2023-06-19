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
    connect(ui->ConfigFileEdit2, &QPlainTextEdit::modificationChanged, ui->saveRC, &QWidget::setEnabled);

    const QDir cutterRCDirectory = Core()->getIaitoRCDefaultDirectory();
    auto cutterRCFileInfo = QFileInfo(cutterRCDirectory, "rc");
    QString iaitorcPath = cutterRCFileInfo.absoluteFilePath();
    
    ui->cutterRCLoaded->setTextInteractionFlags(Qt::TextBrowserInteraction);
    ui->cutterRCLoaded->setOpenExternalLinks(true);
    ui->cutterRCLoaded->setText(tr("Script is loaded from <a href=\"%1\">%2</a>")
                                .arg(QUrl::fromLocalFile(cutterRCDirectory.absolutePath()).toString(), iaitorcPath.toHtmlEscaped()));
    
    ui->saveRC->setDisabled(true);
    ui->executeNow->button(QDialogButtonBox::Retry)->setText("Execute");
    ui->ConfigFileEdit->clear();
    if(cutterRCFileInfo.exists()){
        QFile f(iaitorcPath);
        if(f.open(QIODevice::ReadWrite | QIODevice::Text)){ 
            ui->ConfigFileEdit->setPlainText(f.readAll()); 
        }
        f.close();
    }
    ui->ConfigFileEdit2->clear();
    auto rc2 = QFileInfo(cutterRCDirectory, "rc2");
    if(rc2.exists()){
        QString rc2file = rc2.absoluteFilePath();
        QFile f(rc2file);
        if(f.open(QIODevice::ReadWrite | QIODevice::Text)){ 
            ui->ConfigFileEdit2->setPlainText(f.readAll()); 
        }
        f.close();
    }
}

InitializationFileEditor::~InitializationFileEditor() {};


void InitializationFileEditor::saveIaitoRC(){
    const QDir cutterRCDirectory = Core()->getIaitoRCDefaultDirectory();
    if(!cutterRCDirectory.exists()){
        cutterRCDirectory.mkpath(".");
    }
    auto cutterRCFileInfo = QFileInfo(cutterRCDirectory, "rc");
    QString iaitorcPath = cutterRCFileInfo.absoluteFilePath();
   
    QFile cutterRC(iaitorcPath);
    if(cutterRC.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)){ 
        QTextStream out(&cutterRC);
        QString text = ui->ConfigFileEdit->toPlainText();
        out << text;
        cutterRC.close();
    }
    ui->ConfigFileEdit->document()->setModified(false);

    // save second thing
    auto rc2 = QFileInfo(cutterRCDirectory, "rc2");
    QString rc2file = rc2 .absoluteFilePath();
   
    QFile f(rc2file);
    if(f.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)){ 
        QTextStream out(&f);
        QString text = ui->ConfigFileEdit2->toPlainText();
        out << text;
        f.close();
    }
    ui->ConfigFileEdit2->document()->setModified(false);
}

void InitializationFileEditor::executeIaitoRC(){
    saveIaitoRC();
    Core()->loadDefaultIaitoRC();
    Core()->loadSecondaryIaitoRC();
}
