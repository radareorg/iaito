
#include <QFileDialog>

#include "SaveProjectDialog.h"
#include "common/Configuration.h"
#include "common/TempConfig.h"
#include "core/Iaito.h"
#include "ui_SaveProjectDialog.h"

SaveProjectDialog::SaveProjectDialog(bool quit, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SaveProjectDialog)
{
    ui->setupUi(this);

    IaitoCore *core = Core();

    if (quit) {
        ui->buttonBox->setStandardButtons(
            QDialogButtonBox::Save | QDialogButtonBox::Discard | QDialogButtonBox::Cancel);
    } else {
        ui->buttonBox->setStandardButtons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    }

    ui->nameEdit->setText(core->getConfig("prj.name"));
    ui->projectsDirEdit->setText(Config()->getDirProjects());
    ui->filesCheckBox->setChecked(core->getConfigb("prj.files"));
    ui->gitCheckBox->setChecked(core->getConfigb("prj.vc"));
    ui->zipCheckBox->setChecked(core->getConfigb("prj.zip"));
}

SaveProjectDialog::~SaveProjectDialog() {}

void SaveProjectDialog::on_selectProjectsDirButton_clicked()
{
    QString currentDir = ui->projectsDirEdit->text();
    if (currentDir.startsWith("~")) {
        currentDir = QDir::homePath() + currentDir.mid(1);
    }

    const QString &dir = QDir::toNativeSeparators(
        QFileDialog::getExistingDirectory(this, tr("Select project path (dir.projects)"), currentDir));

    if (!dir.isEmpty()) {
        ui->projectsDirEdit->setText(dir);
    }
}

void SaveProjectDialog::on_buttonBox_clicked(QAbstractButton *button)
{
    if (QDialogButtonBox::DestructiveRole == ui->buttonBox->buttonRole(button)) {
        QDialog::done(QDialog::Accepted);
    }
}

void SaveProjectDialog::accept()
{
    const QString &projectName = ui->nameEdit->text().trimmed();
    if (!IaitoCore::isProjectNameValid(projectName)) {
        QMessageBox::critical(this, tr("Save project"), tr("Invalid project name."));
        ui->nameEdit->setFocus();
        return;
    }
    TempConfig tempConfig;
    Config()->setDirProjects(ui->projectsDirEdit->text().toUtf8().constData());
    tempConfig.set("dir.projects", Config()->getDirProjects())
        .set("prj.files", ui->filesCheckBox->isChecked())
        .set("prj.vc", ui->gitCheckBox->isChecked())
        .set("prj.zip", ui->zipCheckBox->isChecked());

    Core()->saveProject(projectName);

    QDialog::accept();
}
