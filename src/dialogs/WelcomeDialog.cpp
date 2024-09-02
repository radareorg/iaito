#include "core/MainWindow.h"
#include "IaitoConfig.h"

#include "common/Helpers.h"
#include "WelcomeDialog.h"
#include "AboutDialog.h"

#include "ui_WelcomeDialog.h"

/**
 * @brief Constructs a WelcomeDialog object
 * @param parent
 */
WelcomeDialog::WelcomeDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::WelcomeDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
    ui->logoSvgWidget->load(Config()->getLogoFile());
    ui->versionLabel->setText("<font color='#a4a9b2'>" + tr("Version ") + IAITO_VERSION_FULL + "</font>");
    ui->themeComboBox->setCurrentIndex(Config()->getInterfaceTheme());

    QStringList langs = Config()->getAvailableTranslations();
    ui->languageComboBox->addItems(langs);
    QString curr = Config()->getCurrLocale().nativeLanguageName();
    if (!curr.isEmpty()) {
        curr = curr.at(0).toUpper() + curr.right(curr.length() - 1);
    }
    if (!curr.isEmpty() && !langs.contains(curr)) {
        curr = "English";
    }
    if (!curr.isEmpty()) {
        ui->languageComboBox->setCurrentText(curr);
    }
    connect(ui->languageComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            &WelcomeDialog::onLanguageComboBox_currentIndexChanged);

    Config()->adjustColorThemeDarkness();
}

/**
 * @brief Destroys the WelcomeDialog
 */
WelcomeDialog::~WelcomeDialog()
{
    delete ui;
}

/**
 * @brief change Iaito's QT Theme as selected by the user
 * @param index - a Slot being called after theme's value changes its index
 */
void WelcomeDialog::on_themeComboBox_currentIndexChanged(int index)
{
    Config()->setInterfaceTheme(index);

    // make sure that Iaito's logo changes its color according to the selected theme
    ui->logoSvgWidget->load(Config()->getLogoFile());
}

/**
 * @brief change Iaito's interface language as selected by the user
 * @param index - a Slot being called after language combo box value changes its index
 */
void WelcomeDialog::onLanguageComboBox_currentIndexChanged(int index)
{
    QString language = ui->languageComboBox->itemText(index).toLower();
    Config()->setLocaleByName(language);

    QMessageBox mb;
    mb.setWindowTitle(tr("Language settings"));
    mb.setText(tr("Language will be changed after next application start."));
    mb.setIcon(QMessageBox::Information);
    mb.setStandardButtons(QMessageBox::Ok);
    mb.exec();
}

/**
 * @brief show Iaito's About dialog
 */
void WelcomeDialog::on_checkUpdateButton_clicked()
{
    AboutDialog *a = new AboutDialog(this);
    a->setAttribute(Qt::WA_DeleteOnClose);
    a->open();
}

/**
 * @brief accept user preferences, close the window and continue Iaito's execution
 */
void WelcomeDialog::on_continueButton_clicked()
{
    accept();
}

void WelcomeDialog::on_updatesCheckBox_stateChanged(int)
{
    Config()->setAutoUpdateEnabled(!Config()->getAutoUpdateEnabled());
}
