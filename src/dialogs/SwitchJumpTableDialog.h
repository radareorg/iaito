#ifndef SWITCHJUMPTABLEDIALOG_H
#define SWITCHJUMPTABLEDIALOG_H

#include "core/IaitoCommon.h"

#include <QDialog>

class MainWindow;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

class SwitchJumpTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SwitchJumpTableDialog(MainWindow *mainWindow);
    ~SwitchJumpTableDialog() override;

    void setSwitchAddress(RVA addr);

private slots:
    void onApply();
    void onUnsetOverride();
    void onAddBasicBlock();
    void onAddEdge();
    void onSeekToCase();
    void onRefresh();
    void onJmptblEnabledChanged(bool enabled);
    void onJmptblSplitChanged(bool enabled);
    void onJmptblMaxCasesChanged(int value);
    void onCaseDoubleClicked(int row, int column);

private:
    void buildUi();
    void wireSignals();
    void loadFromCore();
    void loadGlobalsFromConfig();
    QString buildAfbtArgs() const;
    void populateCases(const QJsonArray &cases);
    void clearCasesTable();
    void setStatus(const QString &message, bool error = false);

    static QString hexAddr(quint64 v);
    static quint64 parseAddr(const QString &text, bool *ok = nullptr);

    MainWindow *mainWindow = nullptr;
    RVA dispatcherAddr = RVA_INVALID;

    QLabel *dispatcherLabel = nullptr;
    QLineEdit *jtblEdit = nullptr;
    QComboBox *esizeCombo = nullptr;
    QSpinBox *ncasesSpin = nullptr;
    QSpinBox *shiftSpin = nullptr;
    QLineEdit *baseEdit = nullptr;
    QLineEdit *defaultEdit = nullptr;
    QLineEdit *regEdit = nullptr;
    QSpinBox *lowcaseSpin = nullptr;
    QLineEdit *vtblEdit = nullptr;
    QComboBox *vsizeCombo = nullptr;

    QCheckBox *flagSigned = nullptr;
    QCheckBox *flagSubtract = nullptr;
    QCheckBox *flagInsn = nullptr;
    QCheckBox *flagSelfRel = nullptr;
    QCheckBox *flagIndirect = nullptr;
    QCheckBox *flagSparse = nullptr;
    QCheckBox *flagInverse = nullptr;
    QCheckBox *flagDefInTbl = nullptr;
    QCheckBox *flagUserPin = nullptr;

    QCheckBox *globalEnabled = nullptr;
    QCheckBox *globalSplit = nullptr;
    QSpinBox *globalMaxCases = nullptr;

    QTableWidget *casesTable = nullptr;
    QLabel *statusLabel = nullptr;

    QPushButton *applyBtn = nullptr;
    QPushButton *unsetBtn = nullptr;
    QPushButton *addBbBtn = nullptr;
    QPushButton *addEdgeBtn = nullptr;
    QPushButton *seekCaseBtn = nullptr;
    QPushButton *refreshBtn = nullptr;
};

#endif // SWITCHJUMPTABLEDIALOG_H
