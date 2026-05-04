#ifndef CODEINFOWIDGET_H
#define CODEINFOWIDGET_H

#include "IaitoDockWidget.h"
#include "core/Iaito.h"

#include <QJsonObject>
#include <QToolButton>

class MainWindow;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QVBoxLayout;
class QWidget;
class QScrollArea;

class CodeInfoSection : public QWidget
{
    Q_OBJECT

public:
    explicit CodeInfoSection(const QString &title, QWidget *parent = nullptr);

    QWidget *contentArea() const { return content; }
    void setExpanded(bool expanded);
    bool isExpanded() const;

private:
    QToolButton *toggle;
    QWidget *content;
};

class CodeInfoWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit CodeInfoWidget(MainWindow *main);
    ~CodeInfoWidget() override;

private slots:
    void refresh();
    void onPatchInstruction();
    void onPatchBytes();
    void onCopyBytes();
    void onCopyDisasm();
    void onCopyOpcodeJson();
    void onFunctionUndefine();
    void onFunctionAddComment();
    void onFunctionPinPicked(const QString &emoji);
    void onFunctionColorPicked(const QString &r2Color);
    void onBlockColorPicked(const QString &r2Color);

private:
    void buildUi();
    void populateTree(QTreeWidget *tree, const QJsonValue &value);
    void addJsonNode(QTreeWidgetItem *parent, const QString &key, const QJsonValue &value);

    CodeInfoSection *opcodeSection;
    CodeInfoSection *blockSection;
    CodeInfoSection *functionSection;

    QLabel *opcodeAddrLabel;
    QLabel *blockAddrLabel;
    QLabel *functionAddrLabel;

    QLabel *opcodeStatus;
    QLabel *blockStatus;
    QLabel *functionStatus;

    QTreeWidget *opcodeTree;
    QTreeWidget *blockTree;
    QTreeWidget *functionTree;

    QPushButton *btnPatchInstr;
    QPushButton *btnPatchBytes;
    QPushButton *btnCopyBytes;
    QPushButton *btnCopyDisasm;
    QPushButton *btnCopyJson;

    QString currentBytes;
    QString currentDisasm;
    QString currentOpcodeJson;

    RVA currentFunctionAddr;
    RVA currentBlockAddr;
};

#endif // CODEINFOWIDGET_H
