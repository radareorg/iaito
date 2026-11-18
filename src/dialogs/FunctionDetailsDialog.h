#ifndef FUNCTIONDETAILSDIALOG_H
#define FUNCTIONDETAILSDIALOG_H

#include "common/ResponsiveDialog.h"
#include "core/IaitoCommon.h"

#include <QJsonObject>
#include <QMap>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;
class QTreeWidget;

class FunctionDetailsDialog : public ResponsiveDialog
{
    Q_OBJECT

public:
    explicit FunctionDetailsDialog(RVA address, QWidget *parent = nullptr);
    static FunctionDetailsDialog *showFunction(RVA address, QWidget *parent = nullptr);

private slots:
    void refresh();
    void apply();
    void markDirty();
    void refreshIfClean();

private:
    RVA functionAddress;
    bool loading = false;
    bool dirty = false;
    bool available = false;
    QJsonObject originalInfo;
    QString originalSignature;
    QString originalAttributes;
    QLineEdit *nameEdit;
    QLineEdit *stackEdit;
    QComboBox *callingConvention;
    QPlainTextEdit *signatureEdit;
    QTableWidget *variables;
    QTreeWidget *information;
    QMap<QChar, QCheckBox *> attributes;
    QLabel *status;
    QWidget *editor;
    QDialogButtonBox *buttons;

    QJsonObject functionInfo() const;
    void setError(const QString &message);
};

#endif // FUNCTIONDETAILSDIALOG_H
