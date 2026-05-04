#include "dialogs/SwitchJumpTableDialog.h"

#include "core/Iaito.h"
#include "core/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

SwitchJumpTableDialog::SwitchJumpTableDialog(MainWindow *mainWindow)
    : QDialog(mainWindow)
    , mainWindow(mainWindow)
{
    setObjectName(QStringLiteral("switchJumpTableDialog"));
    setWindowTitle(tr("Switch / Jump Table"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(900, 640);

    buildUi();
    wireSignals();
    loadGlobalsFromConfig();
    setSwitchAddress(RVA_INVALID);
}

SwitchJumpTableDialog::~SwitchJumpTableDialog() = default;

void SwitchJumpTableDialog::setSwitchAddress(RVA addr)
{
    dispatcherAddr = (addr == RVA_INVALID) ? Core()->getOffset() : addr;
    dispatcherLabel->setText(hexAddr(dispatcherAddr));
    loadFromCore();
}

void SwitchJumpTableDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);

    // ----- top: spec form ------------------------------------------------
    auto *specPage = new QWidget(splitter);
    auto *specLayout = new QHBoxLayout(specPage);

    auto *specBox = new QGroupBox(tr("Switch specification"), specPage);
    auto *specForm = new QFormLayout(specBox);

    dispatcherLabel = new QLabel(QStringLiteral("0x0"), specBox);
    dispatcherLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    specForm->addRow(tr("Dispatcher (startea)"), dispatcherLabel);

    jtblEdit = new QLineEdit(specBox);
    jtblEdit->setPlaceholderText(QStringLiteral("0x..."));
    jtblEdit->setToolTip(tr("Jump table base address (afbt tbl=)"));
    specForm->addRow(tr("Jump table addr"), jtblEdit);

    esizeCombo = new QComboBox(specBox);
    esizeCombo->addItems(
        {QStringLiteral("4"), QStringLiteral("8"), QStringLiteral("2"), QStringLiteral("1")});
    esizeCombo->setToolTip(tr("Entry size in bytes (afbt esz=)"));
    specForm->addRow(tr("Entry size"), esizeCombo);

    ncasesSpin = new QSpinBox(specBox);
    ncasesSpin->setRange(0, 4096);
    ncasesSpin->setToolTip(tr("0 = autodetect (afbt n=)"));
    specForm->addRow(tr("Number of cases"), ncasesSpin);

    shiftSpin = new QSpinBox(specBox);
    shiftSpin->setRange(0, 3);
    shiftSpin->setToolTip(tr("Left-shift applied to each entry (afbt shift=)"));
    specForm->addRow(tr("Shift"), shiftSpin);

    baseEdit = new QLineEdit(specBox);
    baseEdit->setPlaceholderText(tr("(empty = jtbl addr)"));
    baseEdit->setToolTip(tr("Element base; sets the BASE flag (afbt base=)"));
    specForm->addRow(tr("Base"), baseEdit);

    defaultEdit = new QLineEdit(specBox);
    defaultEdit->setPlaceholderText(tr("(empty = unknown)"));
    defaultEdit->setToolTip(tr("Default-jump address (afbt def=)"));
    specForm->addRow(tr("Default target"), defaultEdit);

    regEdit = new QLineEdit(specBox);
    regEdit->setPlaceholderText(tr("e.g. eax / r0"));
    regEdit->setToolTip(tr("Input register name (afbt reg=)"));
    specForm->addRow(tr("Input register"), regEdit);

    lowcaseSpin = new QSpinBox(specBox);
    lowcaseSpin->setRange(-32768, 32767);
    lowcaseSpin->setToolTip(tr("First input value / case-number shift (afbt low=)"));
    specForm->addRow(tr("Low case"), lowcaseSpin);

    vtblEdit = new QLineEdit(specBox);
    vtblEdit->setPlaceholderText(tr("(only with indir / sparse)"));
    vtblEdit->setToolTip(tr("Value/index table address (afbt vtbl=)"));
    specForm->addRow(tr("Value table addr"), vtblEdit);

    vsizeCombo = new QComboBox(specBox);
    vsizeCombo->addItems({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("4")});
    vsizeCombo->setToolTip(tr("Value-table element size (afbt vsz=)"));
    specForm->addRow(tr("Value entry size"), vsizeCombo);

    specLayout->addWidget(specBox, 2);

    // flags column
    auto *flagsBox = new QGroupBox(tr("Flags"), specPage);
    auto *flagsLayout = new QVBoxLayout(flagsBox);
    auto mkFlag = [&](const QString &label, const QString &tip) {
        auto *cb = new QCheckBox(label, flagsBox);
        cb->setToolTip(tip);
        flagsLayout->addWidget(cb);
        return cb;
    };
    flagSigned = mkFlag(tr("Signed entries"), tr("Sign-extend table entries (afbt signed=1)"));
    flagSubtract = mkFlag(tr("Subtract"), tr("target = base - entry  (afbt sub=1)"));
    flagInsn = mkFlag(tr("Inline branch"), tr("Each entry IS an instruction (afbt insn=1)"));
    flagSelfRel = mkFlag(tr("Self-relative"), tr("target = entry_addr + entry (afbt rel=1)"));
    flagIndirect
        = mkFlag(tr("Indirect (vtbl)"), tr("idx = vtbl[input]; target = jtbl[idx] (afbt indir=1)"));
    flagSparse = mkFlag(tr("Sparse keys"), tr("vtbl[] holds case keys (afbt sparse=1)"));
    flagInverse = mkFlag(tr("Reverse walk"), tr("Walk the table in reverse (afbt inv=1)"));
    flagDefInTbl
        = mkFlag(tr("Default in table"), tr("First/last entry is the default case (afbt dtbl=1)"));
    flagUserPin = mkFlag(
        tr("Pin override (user)"), tr("Persist override; survives auto-analysis (afbt user=1)"));
    flagUserPin->setChecked(true);
    flagsLayout->addStretch(1);
    specLayout->addWidget(flagsBox, 1);

    // global settings on the right
    auto *globalsBox = new QGroupBox(tr("anal.jmptbl globals"), specPage);
    auto *globalsForm = new QFormLayout(globalsBox);
    globalEnabled = new QCheckBox(tr("Analyze jump tables"), globalsBox);
    globalEnabled->setToolTip(tr("anal.jmptbl"));
    globalsForm->addRow(globalEnabled);
    globalSplit = new QCheckBox(tr("Split blocks during walk"), globalsBox);
    globalSplit->setToolTip(tr("anal.jmptbl.split"));
    globalsForm->addRow(globalSplit);
    globalMaxCases = new QSpinBox(globalsBox);
    globalMaxCases->setRange(1, 65535);
    globalMaxCases->setToolTip(tr("anal.jmptbl.maxcases"));
    globalsForm->addRow(tr("Max cases"), globalMaxCases);
    auto *globalsSpacer = new QWidget(globalsBox);
    globalsSpacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    globalsForm->addRow(globalsSpacer);
    specLayout->addWidget(globalsBox, 1);

    splitter->addWidget(specPage);

    // ----- bottom: cases table ------------------------------------------
    auto *casesBox = new QGroupBox(tr("Resolved cases"), splitter);
    auto *casesLayout = new QVBoxLayout(casesBox);
    casesTable = new QTableWidget(0, 4, casesBox);
    casesTable->setHorizontalHeaderLabels({tr("#"), tr("Value"), tr("Entry addr"), tr("Target")});
    casesTable->horizontalHeader()->setStretchLastSection(true);
    casesTable->verticalHeader()->setVisible(false);
    casesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    casesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    casesLayout->addWidget(casesTable);
    splitter->addWidget(casesBox);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    // ----- action bar ----------------------------------------------------
    auto *actionBar = new QHBoxLayout();
    applyBtn = new QPushButton(tr("Apply (afbt)"), this);
    applyBtn->setDefault(true);
    actionBar->addWidget(applyBtn);
    unsetBtn = new QPushButton(tr("Unset override"), this);
    actionBar->addWidget(unsetBtn);
    addBbBtn = new QPushButton(tr("Add basic block..."), this);
    actionBar->addWidget(addBbBtn);
    addEdgeBtn = new QPushButton(tr("Add case edge..."), this);
    actionBar->addWidget(addEdgeBtn);
    seekCaseBtn = new QPushButton(tr("Seek to case"), this);
    actionBar->addWidget(seekCaseBtn);
    refreshBtn = new QPushButton(tr("Refresh"), this);
    actionBar->addWidget(refreshBtn);
    actionBar->addStretch(1);
    root->addLayout(actionBar);

    statusLabel = new QLabel(this);
    statusLabel->setWordWrap(true);
    root->addWidget(statusLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    root->addWidget(buttons);
}

void SwitchJumpTableDialog::wireSignals()
{
    connect(applyBtn, &QPushButton::clicked, this, &SwitchJumpTableDialog::onApply);
    connect(unsetBtn, &QPushButton::clicked, this, &SwitchJumpTableDialog::onUnsetOverride);
    connect(addBbBtn, &QPushButton::clicked, this, &SwitchJumpTableDialog::onAddBasicBlock);
    connect(addEdgeBtn, &QPushButton::clicked, this, &SwitchJumpTableDialog::onAddEdge);
    connect(seekCaseBtn, &QPushButton::clicked, this, &SwitchJumpTableDialog::onSeekToCase);
    connect(refreshBtn, &QPushButton::clicked, this, &SwitchJumpTableDialog::onRefresh);
    connect(globalEnabled, &QCheckBox::toggled, this, &SwitchJumpTableDialog::onJmptblEnabledChanged);
    connect(globalSplit, &QCheckBox::toggled, this, &SwitchJumpTableDialog::onJmptblSplitChanged);
    connect(
        globalMaxCases,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        &SwitchJumpTableDialog::onJmptblMaxCasesChanged);
    connect(
        casesTable,
        &QTableWidget::cellDoubleClicked,
        this,
        &SwitchJumpTableDialog::onCaseDoubleClicked);
}

void SwitchJumpTableDialog::loadGlobalsFromConfig()
{
    QSignalBlocker b1(globalEnabled);
    QSignalBlocker b2(globalSplit);
    QSignalBlocker b3(globalMaxCases);
    globalEnabled->setChecked(Core()->getConfigb("anal.jmptbl"));
    globalSplit->setChecked(Core()->getConfigb("anal.jmptbl.split"));
    globalMaxCases->setValue(Core()->getConfigi("anal.jmptbl.maxcases"));
}

void SwitchJumpTableDialog::loadFromCore()
{
    clearCasesTable();
    if (dispatcherAddr == RVA_INVALID) {
        return;
    }
    QString cmd = QStringLiteral("afbtj @ %1").arg(hexAddr(dispatcherAddr));
    QString out = Core()->cmdRaw(cmd).trimmed();
    if (out.isEmpty() || out == QLatin1String("{}")) {
        setStatus(tr("No switch defined at %1. Fill the form and press Apply to create one.")
                      .arg(hexAddr(dispatcherAddr)));
        return;
    }
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        setStatus(tr("Could not parse afbtj output: %1").arg(err.errorString()), true);
        return;
    }
    QJsonObject obj = doc.object();

    auto setLine = [](QLineEdit *e, quint64 v, bool empty_when_zero = false) {
        if (empty_when_zero && v == 0) {
            e->clear();
        } else if (v == UT64_MAX) {
            e->clear();
        } else {
            e->setText(hexAddr(v));
        }
    };

    setLine(jtblEdit, obj.value("jtbl_addr").toVariant().toULongLong());
    setLine(baseEdit, obj.value("base").toVariant().toULongLong(), true);
    setLine(defaultEdit, obj.value("def").toVariant().toULongLong());
    setLine(vtblEdit, obj.value("vtbl_addr").toVariant().toULongLong(), true);

    const int esize = obj.value("esize").toInt();
    int eidx = esizeCombo->findText(QString::number(esize ? esize : 4));
    if (eidx >= 0) {
        esizeCombo->setCurrentIndex(eidx);
    }
    const int vsize = obj.value("vsize").toInt();
    int vidx = vsizeCombo->findText(QString::number(vsize ? vsize : 1));
    if (vidx >= 0) {
        vsizeCombo->setCurrentIndex(vidx);
    }

    ncasesSpin->setValue(obj.value("ncases").toInt());
    shiftSpin->setValue(obj.value("shift").toInt());
    lowcaseSpin->setValue(obj.value("lowcase").toVariant().toLongLong());
    regEdit->setText(obj.value("reg").toString());

    const quint32 flags = obj.value("flags").toVariant().toUInt();
    flagSigned->setChecked(flags & (1u << 0));
    flagSubtract->setChecked(flags & (1u << 1));
    flagInsn->setChecked(flags & (1u << 2));
    flagSelfRel->setChecked(flags & (1u << 3));
    flagIndirect->setChecked(flags & (1u << 4));
    flagSparse->setChecked(flags & (1u << 5));
    flagInverse->setChecked(flags & (1u << 6));
    flagDefInTbl->setChecked(flags & (1u << 7));
    flagUserPin->setChecked(flags & (1u << 8));

    populateCases(obj.value("cases").toArray());
    setStatus(tr("Loaded switch at %1 (%2 cases).")
                  .arg(hexAddr(dispatcherAddr))
                  .arg(obj.value("ncases").toInt()));
}

QString SwitchJumpTableDialog::buildAfbtArgs() const
{
    QStringList parts;
    bool ok = false;
    quint64 v = parseAddr(jtblEdit->text(), &ok);
    if (ok) {
        parts << QStringLiteral("tbl=0x%1").arg(v, 0, 16);
    }
    parts << QStringLiteral("esz=%1").arg(esizeCombo->currentText());
    parts << QStringLiteral("n=%1").arg(ncasesSpin->value());
    if (shiftSpin->value() != 0) {
        parts << QStringLiteral("shift=%1").arg(shiftSpin->value());
    }
    v = parseAddr(baseEdit->text(), &ok);
    if (ok) {
        parts << QStringLiteral("base=0x%1").arg(v, 0, 16);
    }
    v = parseAddr(defaultEdit->text(), &ok);
    if (ok) {
        parts << QStringLiteral("def=0x%1").arg(v, 0, 16);
    }
    if (!regEdit->text().trimmed().isEmpty()) {
        parts << QStringLiteral("reg=%1").arg(regEdit->text().trimmed());
    }
    if (lowcaseSpin->value() != 0) {
        parts << QStringLiteral("low=%1").arg(lowcaseSpin->value());
    }
    v = parseAddr(vtblEdit->text(), &ok);
    if (ok) {
        parts << QStringLiteral("vtbl=0x%1").arg(v, 0, 16);
        parts << QStringLiteral("vsz=%1").arg(vsizeCombo->currentText());
    }

    auto flag = [&](QCheckBox *cb, const char *name) {
        if (cb->isChecked()) {
            parts << QStringLiteral("%1=1").arg(QLatin1String(name));
        }
    };
    flag(flagSigned, "signed");
    flag(flagSubtract, "sub");
    flag(flagInsn, "insn");
    flag(flagSelfRel, "rel");
    flag(flagIndirect, "indir");
    flag(flagSparse, "sparse");
    flag(flagInverse, "inv");
    flag(flagDefInTbl, "dtbl");
    flag(flagUserPin, "user");

    return parts.join(QChar(' '));
}

void SwitchJumpTableDialog::onApply()
{
    bool ok = false;
    parseAddr(jtblEdit->text(), &ok);
    if (!ok) {
        setStatus(tr("Jump table address is required."), true);
        return;
    }
    const QString args = buildAfbtArgs();
    const QString cmd = QStringLiteral("afbt %1 @ %2").arg(args, hexAddr(dispatcherAddr));
    const QString out = Core()->cmdRaw(cmd).trimmed();
    if (!out.isEmpty()) {
        setStatus(out);
    } else {
        setStatus(tr("Applied: %1").arg(cmd));
    }
    emit Core() -> functionsChanged();
    emit Core() -> refreshCodeViews();
    loadFromCore();
}

void SwitchJumpTableDialog::onUnsetOverride()
{
    Core()->cmdRaw(QStringLiteral("afbt- @ %1").arg(hexAddr(dispatcherAddr)));
    setStatus(tr("Removed user-pinned override at %1.").arg(hexAddr(dispatcherAddr)));
    emit Core() -> functionsChanged();
    emit Core() -> refreshCodeViews();
    loadFromCore();
}

void SwitchJumpTableDialog::onAddBasicBlock()
{
    const QString fcnAddrText = QInputDialog::getText(
        this,
        tr("Add basic block"),
        tr("Function start address (afb+ fcn_at):"),
        QLineEdit::Normal,
        Core()->cmdRaw(QStringLiteral("?vi $FB @ %1").arg(hexAddr(dispatcherAddr))).trimmed());
    if (fcnAddrText.isEmpty()) {
        return;
    }
    bool ok = false;
    const quint64 fcnAddr = parseAddr(fcnAddrText, &ok);
    if (!ok) {
        setStatus(tr("Invalid function address."), true);
        return;
    }
    const QString bbAddrText
        = QInputDialog::getText(this, tr("Add basic block"), tr("New basic block address (bbat):"));
    const quint64 bbAddr = parseAddr(bbAddrText, &ok);
    if (!ok) {
        setStatus(tr("Invalid basic block address."), true);
        return;
    }
    const int bbSize = QInputDialog::getInt(
        this, tr("Add basic block"), tr("Basic block size in bytes (bbsz):"), 1, 1, 1 << 20);
    const QString jumpText = QInputDialog::getText(
        this, tr("Add basic block"), tr("Jump target (optional, leave empty to skip):"));
    const QString failText = QInputDialog::getText(
        this, tr("Add basic block"), tr("Fail target (optional, leave empty to skip):"));

    QString cmd
        = QStringLiteral("afb+ 0x%1 0x%2 %3").arg(fcnAddr, 0, 16).arg(bbAddr, 0, 16).arg(bbSize);
    if (!jumpText.trimmed().isEmpty()) {
        const quint64 j = parseAddr(jumpText, &ok);
        if (ok) {
            cmd += QStringLiteral(" 0x%1").arg(j, 0, 16);
            if (!failText.trimmed().isEmpty()) {
                const quint64 f = parseAddr(failText, &ok);
                if (ok) {
                    cmd += QStringLiteral(" 0x%1").arg(f, 0, 16);
                }
            }
        }
    }
    const QString out = Core()->cmdRaw(cmd).trimmed();
    setStatus(out.isEmpty() ? tr("Basic block added: %1").arg(cmd) : out, !out.isEmpty());
    emit Core() -> functionsChanged();
    emit Core() -> refreshCodeViews();
    loadFromCore();
}

void SwitchJumpTableDialog::onAddEdge()
{
    bool ok = false;
    const QString fromText = QInputDialog::getText(
        this,
        tr("Add case edge"),
        tr("Switch BB address (afbe bbfrom):"),
        QLineEdit::Normal,
        hexAddr(dispatcherAddr));
    const quint64 from = parseAddr(fromText, &ok);
    if (!ok) {
        setStatus(tr("Invalid bbfrom address."), true);
        return;
    }
    const QString toText
        = QInputDialog::getText(this, tr("Add case edge"), tr("Case BB address (afbe bbto):"));
    const quint64 to = parseAddr(toText, &ok);
    if (!ok) {
        setStatus(tr("Invalid bbto address."), true);
        return;
    }
    const QString cmd = QStringLiteral("afbe 0x%1 0x%2").arg(from, 0, 16).arg(to, 0, 16);
    const QString out = Core()->cmdRaw(cmd).trimmed();
    setStatus(out.isEmpty() ? tr("Edge added: %1").arg(cmd) : out, !out.isEmpty());
    emit Core() -> functionsChanged();
    emit Core() -> refreshCodeViews();
    loadFromCore();
}

void SwitchJumpTableDialog::onSeekToCase()
{
    const int row = casesTable->currentRow();
    if (row < 0) {
        setStatus(tr("Select a case row first."), true);
        return;
    }
    auto *targetItem = casesTable->item(row, 3);
    if (!targetItem) {
        return;
    }
    bool ok = false;
    const quint64 target = parseAddr(targetItem->text(), &ok);
    if (ok) {
        Core()->seekAndShow(target);
    }
}

void SwitchJumpTableDialog::onRefresh()
{
    loadGlobalsFromConfig();
    loadFromCore();
}

void SwitchJumpTableDialog::onJmptblEnabledChanged(bool enabled)
{
    Core()->setConfig("anal.jmptbl", enabled);
}

void SwitchJumpTableDialog::onJmptblSplitChanged(bool enabled)
{
    Core()->setConfig("anal.jmptbl.split", enabled);
}

void SwitchJumpTableDialog::onJmptblMaxCasesChanged(int value)
{
    Core()->setConfig("anal.jmptbl.maxcases", value);
}

void SwitchJumpTableDialog::onCaseDoubleClicked(int row, int /*column*/)
{
    auto *targetItem = casesTable->item(row, 3);
    if (!targetItem) {
        return;
    }
    bool ok = false;
    const quint64 target = parseAddr(targetItem->text(), &ok);
    if (ok) {
        Core()->seekAndShow(target);
    }
}

void SwitchJumpTableDialog::populateCases(const QJsonArray &cases)
{
    clearCasesTable();
    casesTable->setRowCount(cases.size());
    for (int i = 0; i < cases.size(); i++) {
        QJsonObject c = cases[i].toObject();
        auto *idx = new QTableWidgetItem(QString::number(i));
        auto *value = new QTableWidgetItem(
            QString::number(c.value("value").toVariant().toLongLong()));
        auto *entry = new QTableWidgetItem(hexAddr(c.value("addr").toVariant().toULongLong()));
        auto *target = new QTableWidgetItem(hexAddr(c.value("jump").toVariant().toULongLong()));
        casesTable->setItem(i, 0, idx);
        casesTable->setItem(i, 1, value);
        casesTable->setItem(i, 2, entry);
        casesTable->setItem(i, 3, target);
    }
}

void SwitchJumpTableDialog::clearCasesTable()
{
    casesTable->clearContents();
    casesTable->setRowCount(0);
}

void SwitchJumpTableDialog::setStatus(const QString &message, bool error)
{
    statusLabel->setText(message);
    statusLabel->setStyleSheet(error ? QStringLiteral("color: #c01515;") : QStringLiteral(""));
}

QString SwitchJumpTableDialog::hexAddr(quint64 v)
{
    return QStringLiteral("0x%1").arg(v, 0, 16);
}

quint64 SwitchJumpTableDialog::parseAddr(const QString &text, bool *ok)
{
    const QString s = text.trimmed();
    if (s.isEmpty()) {
        if (ok) {
            *ok = false;
        }
        return 0;
    }
    const quint64 v = Core()->math(s);
    if (ok) {
        *ok = (v != 0);
    }
    return v;
}
