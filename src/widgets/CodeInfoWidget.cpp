#include "CodeInfoWidget.h"
#include "core/MainWindow.h"
#include "dialogs/CommentsDialog.h"
#include "menus/ColorPickerMenu.h"
#include "menus/PinPickerMenu.h"

#include <functional>
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

static void fitTreeToContent(QTreeWidget *tree);

CodeInfoSection::CodeInfoSection(const QString &title, QWidget *parent)
    : QWidget(parent)
    , toggle(new QToolButton(this))
    , content(new QWidget(this))
{
    toggle->setStyleSheet(QStringLiteral("QToolButton { border: none; font-weight: bold; }"));
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::DownArrow);
    toggle->setText(title);
    toggle->setCheckable(true);
    toggle->setChecked(true);

    content->setVisible(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(toggle);
    layout->addWidget(content);

    connect(toggle, &QToolButton::toggled, this, [this](bool checked) {
        toggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        content->setVisible(checked);
    });
}

void CodeInfoSection::setExpanded(bool expanded)
{
    toggle->setChecked(expanded);
}

bool CodeInfoSection::isExpanded() const
{
    return toggle->isChecked();
}

CodeInfoWidget::CodeInfoWidget(MainWindow *main)
    : IaitoDockWidget(main)
    , currentFunctionAddr(RVA_INVALID)
    , currentBlockAddr(RVA_INVALID)
{
    setObjectName(QStringLiteral("CodeInfoWidget"));
    setWindowTitle(tr("CodeInfo"));

    buildUi();

    connect(Core(), &IaitoCore::seekChanged, this, &CodeInfoWidget::refresh);
    connect(Core(), &IaitoCore::refreshAll, this, &CodeInfoWidget::refresh);
    connect(Core(), &IaitoCore::codeRebased, this, &CodeInfoWidget::refresh);
    connect(Core(), &IaitoCore::instructionChanged, this, [this](RVA) { refresh(); });
    connect(Core(), &IaitoCore::functionRenamed, this, [this](const RVA, const QString &) {
        refresh();
    });
    connect(Core(), &IaitoCore::functionsChanged, this, &CodeInfoWidget::refresh);
    connect(Core(), &IaitoCore::commentsChanged, this, &CodeInfoWidget::refresh);
    connect(this, &IaitoDockWidget::becameVisibleToUser, this, &CodeInfoWidget::refresh);
}

CodeInfoWidget::~CodeInfoWidget() = default;

void CodeInfoWidget::buildUi()
{
    auto *root = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(6);

    // Opcode section
    opcodeSection = new CodeInfoSection(tr("Opcode (aoj)"), root);
    {
        auto *layout = new QVBoxLayout(opcodeSection->contentArea());
        layout->setContentsMargins(8, 0, 0, 0);
        layout->setSpacing(2);

        opcodeAddrLabel = new QLabel(tr("address: -"), opcodeSection);
        opcodeAddrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(opcodeAddrLabel);

        auto *btnRow = new QHBoxLayout();
        btnRow->setContentsMargins(0, 0, 0, 0);
        btnRow->setSpacing(4);
        btnPatchInstr = new QPushButton(tr("Reassemble"), opcodeSection);
        btnPatchInstr->setToolTip(tr("Patch instruction text (rasm2)"));
        btnPatchBytes = new QPushButton(tr("Patch Bytes"), opcodeSection);
        btnPatchBytes->setToolTip(tr("Edit raw bytes of the current instruction"));
        btnCopyBytes = new QPushButton(tr("Copy Bytes"), opcodeSection);
        btnCopyDisasm = new QPushButton(tr("Copy Disasm"), opcodeSection);
        btnCopyJson = new QPushButton(tr("Copy JSON"), opcodeSection);
        btnRow->addWidget(btnPatchInstr);
        btnRow->addWidget(btnPatchBytes);
        btnRow->addWidget(btnCopyBytes);
        btnRow->addWidget(btnCopyDisasm);
        btnRow->addWidget(btnCopyJson);
        btnRow->addStretch();
        layout->addLayout(btnRow);

        opcodeStatus = new QLabel(opcodeSection);
        opcodeStatus->setWordWrap(true);
        opcodeStatus->hide();
        layout->addWidget(opcodeStatus);
        opcodeTree = new QTreeWidget(opcodeSection);
        opcodeTree->setColumnCount(2);
        opcodeTree->setHeaderLabels({tr("Field"), tr("Value")});
        opcodeTree->setRootIsDecorated(true);
        opcodeTree->setAlternatingRowColors(true);
        opcodeTree->header()->setStretchLastSection(true);
        opcodeTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        opcodeTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        opcodeTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        layout->addWidget(opcodeTree);
    }
    rootLayout->addWidget(opcodeSection);

    // Basic block section
    blockSection = new CodeInfoSection(tr("Basic Block (abj)"), root);
    {
        auto *layout = new QVBoxLayout(blockSection->contentArea());
        layout->setContentsMargins(8, 0, 0, 0);
        layout->setSpacing(2);
        blockAddrLabel = new QLabel(tr("address: -"), blockSection);
        blockAddrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(blockAddrLabel);

        auto *btnRow = new QHBoxLayout();
        btnRow->setContentsMargins(0, 0, 0, 0);
        btnRow->setSpacing(4);
        auto *btnBlockColor = new QToolButton(blockSection);
        btnBlockColor->setText(tr("Change color"));
        btnBlockColor->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btnBlockColor->setPopupMode(QToolButton::InstantPopup);
        auto *blockColorMenu = new ColorPickerMenu(tr("Change color"), btnBlockColor);
        btnBlockColor->setMenu(blockColorMenu);
        connect(
            blockColorMenu,
            &ColorPickerMenu::colorPicked,
            this,
            &CodeInfoWidget::onBlockColorPicked);
        btnRow->addWidget(btnBlockColor);
        btnRow->addStretch();
        layout->addLayout(btnRow);

        blockStatus = new QLabel(blockSection);
        blockStatus->setWordWrap(true);
        blockStatus->hide();
        layout->addWidget(blockStatus);
        blockTree = new QTreeWidget(blockSection);
        blockTree->setColumnCount(2);
        blockTree->setHeaderLabels({tr("Field"), tr("Value")});
        blockTree->setRootIsDecorated(true);
        blockTree->setAlternatingRowColors(true);
        blockTree->header()->setStretchLastSection(true);
        blockTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        blockTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        blockTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        layout->addWidget(blockTree);
    }
    rootLayout->addWidget(blockSection);

    // Function section
    functionSection = new CodeInfoSection(tr("Function (afij)"), root);
    {
        auto *layout = new QVBoxLayout(functionSection->contentArea());
        layout->setContentsMargins(8, 0, 0, 0);
        layout->setSpacing(2);
        functionAddrLabel = new QLabel(tr("address: -"), functionSection);
        functionAddrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(functionAddrLabel);

        auto *btnRow = new QHBoxLayout();
        btnRow->setContentsMargins(0, 0, 0, 0);
        btnRow->setSpacing(4);

        auto *btnFnPin = new QToolButton(functionSection);
        btnFnPin->setText(tr("Pin"));
        btnFnPin->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btnFnPin->setPopupMode(QToolButton::InstantPopup);
        auto *pinMenu = new PinPickerMenu(tr("Pin function"), btnFnPin);
        btnFnPin->setMenu(pinMenu);
        connect(pinMenu, &PinPickerMenu::pinPicked, this, &CodeInfoWidget::onFunctionPinPicked);
        btnRow->addWidget(btnFnPin);

        auto *btnFnColor = new QToolButton(functionSection);
        btnFnColor->setText(tr("Change color"));
        btnFnColor->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btnFnColor->setPopupMode(QToolButton::InstantPopup);
        auto *fnColorMenu = new ColorPickerMenu(tr("Change function color"), btnFnColor);
        btnFnColor->setMenu(fnColorMenu);
        connect(
            fnColorMenu,
            &ColorPickerMenu::colorPicked,
            this,
            &CodeInfoWidget::onFunctionColorPicked);
        btnRow->addWidget(btnFnColor);

        auto *btnFnUndefine = new QPushButton(tr("Undefine"), functionSection);
        connect(btnFnUndefine, &QPushButton::clicked, this, &CodeInfoWidget::onFunctionUndefine);
        btnRow->addWidget(btnFnUndefine);

        auto *btnFnComment = new QPushButton(tr("Add comment"), functionSection);
        connect(btnFnComment, &QPushButton::clicked, this, &CodeInfoWidget::onFunctionAddComment);
        btnRow->addWidget(btnFnComment);

        btnRow->addStretch();
        layout->addLayout(btnRow);

        functionStatus = new QLabel(functionSection);
        functionStatus->setWordWrap(true);
        functionStatus->hide();
        layout->addWidget(functionStatus);
        functionTree = new QTreeWidget(functionSection);
        functionTree->setColumnCount(2);
        functionTree->setHeaderLabels({tr("Field"), tr("Value")});
        functionTree->setRootIsDecorated(true);
        functionTree->setAlternatingRowColors(true);
        functionTree->header()->setStretchLastSection(true);
        functionTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        functionTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        functionTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        layout->addWidget(functionTree);
    }
    rootLayout->addWidget(functionSection);

    rootLayout->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(root);
    setWidget(scroll);

    connect(btnPatchInstr, &QPushButton::clicked, this, &CodeInfoWidget::onPatchInstruction);
    connect(btnPatchBytes, &QPushButton::clicked, this, &CodeInfoWidget::onPatchBytes);
    connect(btnCopyBytes, &QPushButton::clicked, this, &CodeInfoWidget::onCopyBytes);
    connect(btnCopyDisasm, &QPushButton::clicked, this, &CodeInfoWidget::onCopyDisasm);
    connect(btnCopyJson, &QPushButton::clicked, this, &CodeInfoWidget::onCopyOpcodeJson);

    for (QTreeWidget *t : {opcodeTree, blockTree, functionTree}) {
        connect(t, &QTreeWidget::itemExpanded, this, [t](QTreeWidgetItem *) {
            fitTreeToContent(t);
        });
        connect(t, &QTreeWidget::itemCollapsed, this, [t](QTreeWidgetItem *) {
            fitTreeToContent(t);
        });
    }
}

static QString jsonScalarToString(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        double d = v.toDouble();
        qlonglong i = static_cast<qlonglong>(d);
        if (static_cast<double>(i) == d) {
            return QString::number(i);
        }
        return QString::number(d, 'g', 17);
    }
    case QJsonValue::String:
        return v.toString();
    case QJsonValue::Null:
        return QStringLiteral("null");
    case QJsonValue::Undefined:
        return QStringLiteral("(undefined)");
    default:
        return QString();
    }
}

void CodeInfoWidget::addJsonNode(QTreeWidgetItem *parent, const QString &key, const QJsonValue &value)
{
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, key);

    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        item->setText(1, tr("{%1 fields}").arg(obj.size()));
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            addJsonNode(item, it.key(), it.value());
        }
    } else if (value.isArray()) {
        QJsonArray arr = value.toArray();
        item->setText(1, tr("[%1 items]").arg(arr.size()));
        for (int i = 0; i < arr.size(); ++i) {
            addJsonNode(item, QString::number(i), arr.at(i));
        }
    } else {
        item->setText(1, jsonScalarToString(value));
    }
}

static int countVisibleRows(QTreeWidgetItem *item)
{
    int n = 1;
    if (item->isExpanded()) {
        for (int i = 0; i < item->childCount(); ++i) {
            n += countVisibleRows(item->child(i));
        }
    }
    return n;
}

static void fitTreeToContent(QTreeWidget *tree)
{
    int rows = 0;
    QTreeWidgetItem *root = tree->invisibleRootItem();
    for (int i = 0; i < root->childCount(); ++i) {
        rows += countVisibleRows(root->child(i));
    }
    int rowH = tree->sizeHintForRow(0);
    if (rowH <= 0) {
        rowH = tree->fontMetrics().height() + 4;
    }
    int header = tree->header()->isVisible() ? tree->header()->height() : 0;
    int frame = 2 * tree->frameWidth();
    int height = header + qMax(1, rows) * rowH + frame + 2;
    tree->setFixedHeight(height);
}

void CodeInfoWidget::populateTree(QTreeWidget *tree, const QJsonValue &value)
{
    tree->clear();
    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            addJsonNode(tree->invisibleRootItem(), it.key(), it.value());
        }
    } else if (value.isArray()) {
        QJsonArray arr = value.toArray();
        for (int i = 0; i < arr.size(); ++i) {
            addJsonNode(tree->invisibleRootItem(), QString::number(i), arr.at(i));
        }
    } else if (!value.isNull() && !value.isUndefined()) {
        auto *item = new QTreeWidgetItem(tree->invisibleRootItem());
        item->setText(0, tr("value"));
        item->setText(1, jsonScalarToString(value));
    }
    tree->expandAll();
    fitTreeToContent(tree);
}

static void setSectionStatus(QLabel *status, QTreeWidget *tree, const QString &message)
{
    if (message.isEmpty()) {
        status->hide();
        tree->show();
    } else {
        status->setText(message);
        status->show();
        tree->hide();
    }
}

static QJsonValue firstArrayItem(const QJsonDocument &doc)
{
    if (doc.isArray()) {
        QJsonArray arr = doc.array();
        if (!arr.isEmpty()) {
            return arr.first();
        }
    } else if (doc.isObject()) {
        return doc.object();
    }
    return QJsonValue();
}

void CodeInfoWidget::refresh()
{
    if (!isVisibleToUser()) {
        return;
    }

    const RVA off = Core()->getOffset();
    const QString addrStr = RAddressString(off);

    // Opcode (aoj)
    QJsonDocument opDoc = Core()->cmdj("aoj");
    QJsonValue opVal = firstArrayItem(opDoc);
    populateTree(opcodeTree, opVal);
    opcodeAddrLabel->setText(tr("address: %1").arg(addrStr));
    if (opVal.isObject()) {
        QJsonObject o = opVal.toObject();
        currentBytes = o.value(QStringLiteral("bytes")).toString();
        currentDisasm = o.value(QStringLiteral("disasm")).toString();
        if (currentDisasm.isEmpty()) {
            currentDisasm = o.value(QStringLiteral("opcode")).toString();
        }
        currentOpcodeJson = QString::fromUtf8(opDoc.toJson(QJsonDocument::Indented));
        setSectionStatus(opcodeStatus, opcodeTree, QString());
    } else {
        currentBytes.clear();
        currentDisasm.clear();
        currentOpcodeJson.clear();
        setSectionStatus(opcodeStatus, opcodeTree, tr("No opcode information at this address."));
    }
    const bool hasOpcode = !currentBytes.isEmpty() || !currentDisasm.isEmpty();
    btnPatchInstr->setEnabled(hasOpcode);
    btnPatchBytes->setEnabled(hasOpcode);
    btnCopyBytes->setEnabled(!currentBytes.isEmpty());
    btnCopyDisasm->setEnabled(!currentDisasm.isEmpty());
    btnCopyJson->setEnabled(!currentOpcodeJson.isEmpty());

    // Basic block (abj.)
    QJsonDocument bbDoc = Core()->cmdj("abj.");
    QJsonValue bbVal = firstArrayItem(bbDoc);
    populateTree(blockTree, bbVal);
    if (bbVal.isObject()) {
        const RVA bbAddr = bbVal.toObject().value(QStringLiteral("addr")).toVariant().toULongLong();
        currentBlockAddr = bbAddr;
        blockAddrLabel->setText(tr("address: %1").arg(RAddressString(bbAddr)));
        setSectionStatus(blockStatus, blockTree, QString());
    } else {
        currentBlockAddr = RVA_INVALID;
        blockAddrLabel->setText(tr("address: -"));
        setSectionStatus(
            blockStatus,
            blockTree,
            tr("No basic block at %1. Run analysis (e.g. `aaa`) or define a function with `af`.")
                .arg(addrStr));
    }

    // Function (afij)
    QJsonDocument fnDoc = Core()->cmdj("afij");
    QJsonValue fnVal = firstArrayItem(fnDoc);
    populateTree(functionTree, fnVal);
    if (fnVal.isObject()) {
        QJsonObject o = fnVal.toObject();
        const RVA fnAddr = o.value(QStringLiteral("addr")).toVariant().toULongLong();
        currentFunctionAddr = fnAddr;
        const QString name = o.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            functionAddrLabel->setText(tr("address: %1").arg(RAddressString(fnAddr)));
        } else {
            functionAddrLabel->setText(tr("address: %1 (%2)").arg(RAddressString(fnAddr), name));
        }
        setSectionStatus(functionStatus, functionTree, QString());
    } else {
        currentFunctionAddr = RVA_INVALID;
        functionAddrLabel->setText(tr("address: -"));
        setSectionStatus(
            functionStatus,
            functionTree,
            tr("No function at %1. Run analysis (e.g. `aaa`) or define a function with `af`.")
                .arg(addrStr));
    }
}

void CodeInfoWidget::onPatchInstruction()
{
    if (mainWindow) {
        mainWindow->on_actionPatchInstruction_triggered();
    }
}

void CodeInfoWidget::onPatchBytes()
{
    if (mainWindow) {
        mainWindow->on_actionPatchBytes_triggered();
    }
}

void CodeInfoWidget::onCopyBytes()
{
    if (!currentBytes.isEmpty()) {
        QApplication::clipboard()->setText(currentBytes);
    }
}

void CodeInfoWidget::onCopyDisasm()
{
    if (!currentDisasm.isEmpty()) {
        QApplication::clipboard()->setText(currentDisasm);
    }
}

void CodeInfoWidget::onCopyOpcodeJson()
{
    if (!currentOpcodeJson.isEmpty()) {
        QApplication::clipboard()->setText(currentOpcodeJson);
    }
}

void CodeInfoWidget::onFunctionUndefine()
{
    if (currentFunctionAddr == RVA_INVALID) {
        return;
    }
    Core()->delFunction(currentFunctionAddr);
}

void CodeInfoWidget::onFunctionAddComment()
{
    RVA target = currentFunctionAddr != RVA_INVALID ? currentFunctionAddr : Core()->getOffset();
    CommentsDialog::addOrEditComment(target, this);
}

void CodeInfoWidget::onFunctionPinPicked(const QString &emoji)
{
    if (currentFunctionAddr == RVA_INVALID) {
        return;
    }
    if (emoji.isEmpty()) {
        Core()->cmd(QStringLiteral("aflp- @ %1").arg(currentFunctionAddr));
    } else {
        Core()->cmd(QStringLiteral("aflp %1 @ %2").arg(emoji).arg(currentFunctionAddr));
    }
    emit Core() -> functionsChanged();
    emit Core() -> refreshCodeViews();
}

void CodeInfoWidget::onFunctionColorPicked(const QString &r2Color)
{
    if (currentFunctionAddr == RVA_INVALID) {
        return;
    }
    if (r2Color.isEmpty()) {
        Core()->cmd(QStringLiteral("abc- @ %1").arg(currentFunctionAddr));
    } else {
        Core()->cmd(QStringLiteral("abc %1 @ %2").arg(r2Color).arg(currentFunctionAddr));
    }
    emit Core() -> functionsChanged();
    emit Core() -> refreshCodeViews();
}

void CodeInfoWidget::onBlockColorPicked(const QString &r2Color)
{
    if (currentBlockAddr == RVA_INVALID) {
        return;
    }
    if (r2Color.isEmpty()) {
        Core()->cmd(QStringLiteral("abc- @ %1").arg(currentBlockAddr));
    } else {
        Core()->cmd(QStringLiteral("abc %1 @ %2").arg(r2Color).arg(currentBlockAddr));
    }
    emit Core() -> refreshCodeViews();
}
