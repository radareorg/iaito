#ifndef WELCOMEDIALOG_H
#define WELCOMEDIALOG_H

#include <QDialog>

namespace Ui {

/**
 * @class WelcomeDialog
 * @brief The WelcomeDialog class will show the user the Welcome windows
 *  upon first execution of Iaito.
 *
 * Upon first execution of Iaito, the WelcomeDialog would be showed to the user.
 * The Welcome dialog would be showed after a reset of Iaito's preferences by
 * the user.
 */

class WelcomeDialog;
} // namespace Ui

class WelcomeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WelcomeDialog(QWidget *parent = 0);
    ~WelcomeDialog();

private slots:
    void on_themeComboBox_currentIndexChanged(int index);
    void onLanguageComboBox_currentIndexChanged(int index);
    void on_checkUpdateButton_clicked();
    void on_continueButton_clicked();
    void on_updatesCheckBox_stateChanged(int state);

private:
    Ui::WelcomeDialog *ui;
};

#endif // WELCOMEDIALOG_H
