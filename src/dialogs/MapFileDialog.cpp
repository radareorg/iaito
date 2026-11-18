#include "MapFileDialog.h"
#include "ui_MapFileDialog.h"

#include "common/Configuration.h"
#include "common/FilePaths.h"
#include "core/MainWindow.h"

#include <QFileDialog>

MapFileDialog::MapFileDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MapFileDialog)
{
    ui->setupUi(this);
}

MapFileDialog::~MapFileDialog() {}

void MapFileDialog::on_selectFileButton_clicked()
{
    QString currentDir = Config()->getRecentFolder();
    const QUrl initial = currentDir.contains(QStringLiteral("://"))
                             ? QUrl(currentDir)
                             : QUrl::fromLocalFile(currentDir);
    const QUrl selected = QFileDialog::getOpenFileUrl(
        this, tr("Select file"), initial, QString(), nullptr, QFILEDIALOG_FLAGS);
    const QString fileName = Iaito::fileNameFromUrl(selected);

    if (!fileName.isEmpty()) {
        ui->filenameLineEdit->setText(fileName);
        Config()->setRecentFolder(
            selected.isLocalFile() ? QFileInfo(fileName).absolutePath() : fileName);
    }
}

void MapFileDialog::on_buttonBox_accepted()
{
    const QString filePath = Iaito::fileNameForIO(ui->filenameLineEdit->text(), "file://");
    RVA mapAddress = RVA_INVALID;
    QString mapAddressStr = ui->mapAddressLineEdit->text();
    if (!mapAddressStr.isEmpty()) {
        mapAddress = Core()->math(mapAddressStr);
    }

    if (!Core()->mapFile(filePath, mapAddress)) {
        QMessageBox::critical(this, tr("Map new file file"), tr("Failed to map a new file"));
        return;
    }
    close();
}

void MapFileDialog::on_buttonBox_rejected()
{
    close();
}
