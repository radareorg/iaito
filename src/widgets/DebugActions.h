#pragma once

#include "core/Iaito.h"
#include "dialogs/RemoteDebugDialog.h"

#include <QAction>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QDialogButtonBox>

class MainWindow;
class QToolBar;
class QToolButton;
class TextEditDialog;

class TextEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit TextEditDialog(const QString& initialText, QWidget *parent = nullptr)
        : QDialog(parent), editedText(initialText) {
        setWindowTitle("Edit Text");

        // Create the QTextEdit widget
        textEdit = new QTextEdit(this);
        textEdit->setPlainText(initialText);

        // Create buttons
        QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);

        // Layout setup
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->addWidget(textEdit);
        layout->addWidget(buttonBox);

        // Connect buttons to the appropriate slots
        connect(buttonBox->button(QDialogButtonBox::Save), &QPushButton::clicked, this, &TextEditDialog::accept);
        connect(buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this, &TextEditDialog::reject);
	}

    QString getEditedText() const;

protected:
    void accept() override;

private:
    QTextEdit *textEdit;
    QString editedText;
};

class DebugActions : public QObject
{
    Q_OBJECT

public:
    explicit DebugActions(QToolBar *toolBar, MainWindow *main);

    void addToToolBar(QToolBar *toolBar);

    QAction *rarunProfile;
    QAction *actionStart;
    QAction *actionStartRemote;
    QAction *actionStartEmul;
    QAction *actionAttach;
    QAction *actionContinue;
    QAction *actionContinueUntilMain;
    QAction *actionContinueUntilCall;
    QAction *actionContinueUntilSyscall;
    QAction *actionStep;
    QAction *actionStepOver;
    QAction *actionStepOut;
    QAction *actionStop;
    QAction *actionAllContinues;

    // Continue/suspend and start/restart interchange during runtime
    QIcon continueIcon;
    QIcon suspendIcon;
    QIcon restartIcon;
    QIcon startDebugIcon;
    QString suspendLabel;
    QString continueLabel;
    QString restartDebugLabel;
    QString startDebugLabel;
    QString rarunProfileLabel;

    // Stop and Detach interchange during runtime
    QIcon detachIcon;
    QIcon stopIcon;
    
private:
    /**
     * @brief buttons that will be disabled/enabled on (disable/enable)DebugToolbar
     */
    QList<QAction *> toggleActions;
    QList<QAction *> toggleConnectionActions;
    QList<QAction *> allActions;
    QToolButton *continueUntilButton;
    RemoteDebugDialog *remoteDialog = nullptr;
    MainWindow *main;
    bool acceptedDebugWarning = false;

    // TODO: Remove once debug is stable
    void showDebugWarning();

private slots:
    void continueUntilMain();
    void startDebug();
    void editRarunProfile();
    void attachProcessDialog();
    void attachProcess(int pid);
    void attachRemoteDialog();
    void attachRemoteDebugger();
    void onAttachedRemoteDebugger(bool successfully);
    void setAllActionsVisible(bool visible);
    void setButtonVisibleIfMainExists();
    void chooseThemeIcons();
};
