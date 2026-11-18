#include "FunctionDetailsDialog.h"

#include "core/Iaito.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <climits>
#include <cstdlib>

namespace {
QString jsonText(const QJsonValue &value)
{
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    return value.toVariant().toString();
}

bool validName(const QString &name)
{
    return !name.isEmpty() && !name.contains(QChar::Null)
           && r_name_check(name.toUtf8().constData());
}

// Validate declarations without saving anything to the type database.
bool validDeclaration(
    const QString &declaration, const QString &name, QString &error, QString *result = nullptr)
{
    if (declaration.contains(QChar::Null)) {
        return false;
    }
    auto core = Core()->core();
    char *parseError = nullptr;
    char *parsed = r_anal_cparse(core->anal, declaration.toUtf8().constData(), &parseError);
    const QString output = QString::fromUtf8(parsed ? parsed : "");
    error = QString::fromUtf8(parseError ? parseError : "").trimmed();
    free(parsed);
    free(parseError);
    if (result) {
        *result = output;
    }
    const QString definition = name + QStringLiteral("=func");
    return !output.isEmpty() && error.isEmpty()
           && (output.startsWith(definition) || output.contains(QLatin1Char('\n') + definition));
}
} // namespace

FunctionDetailsDialog::FunctionDetailsDialog(RVA address, QWidget *parent)
    : ResponsiveDialog(parent)
    , functionAddress(address)
{
    setObjectName(QStringLiteral("FunctionDetailsDialog"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowModality(Qt::NonModal);
    resize(680, 680);

    // Resolve interior instructions once. Subsequent seeks never change this window's target.
    {
        auto core = Core()->core();
        if (auto function = r_anal_get_fcn_in(core->anal, address, 0)) {
            functionAddress = function->addr;
        }
    }

    auto layout = new QVBoxLayout(this);
    editor = new QWidget(this);
    auto body = new QVBoxLayout(editor);
    body->setContentsMargins(0, 0, 0, 0);
    auto form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    nameEdit = new QLineEdit(editor);
    nameEdit->setObjectName(QStringLiteral("functionName"));
    form->addRow(tr("Name"), nameEdit);
    callingConvention = new QComboBox(editor);
    callingConvention->setObjectName(QStringLiteral("callingConvention"));
    form->addRow(tr("Calling convention"), callingConvention);
    stackEdit = new QLineEdit(editor);
    stackEdit->setObjectName(QStringLiteral("stackSize"));
    form->addRow(tr("Stack frame size"), stackEdit);
    body->addLayout(form);
    body->addWidget(new QLabel(tr("Function signature (including parameter types)"), editor));
    signatureEdit = new QPlainTextEdit(editor);
    signatureEdit->setObjectName(QStringLiteral("functionSignature"));
    signatureEdit->setTabChangesFocus(true);
    signatureEdit->setMaximumHeight(100);
    body->addWidget(signatureEdit);

    auto attributeBox = new QGroupBox(tr("Attributes"), editor);
    auto attributeLayout = new QGridLayout(attributeBox);
#ifdef R_ANAL_FCN_ATTR_COMPUTED
    for (size_t i = 0; const auto info = r_anal_function_attr_info(i); ++i) {
        const QChar letter = QLatin1Char(info->letter);
        auto checkbox = new QCheckBox(
            QStringLiteral("%1 %2").arg(letter).arg(QString::fromUtf8(info->name)), attributeBox);
        checkbox->setObjectName(QStringLiteral("attribute%1").arg(letter));
        const bool computed = info->bit & R_ANAL_FCN_ATTR_COMPUTED;
        checkbox->setEnabled(!computed);
        checkbox->setToolTip(
            QString::fromUtf8(info->desc)
            + (computed ? tr(" (read-only, computed by analysis)") : QString()));
        attributeLayout->addWidget(checkbox, int(i) / 4, int(i) % 4);
        attributes.insert(letter, checkbox);
        connect(checkbox, &QCheckBox::toggled, this, &FunctionDetailsDialog::markDirty);
    }
#else
    attributeLayout->addWidget(new QLabel(tr("Function attributes require a newer radare2.")));
#endif
    body->addWidget(attributeBox);

    auto tabs = new QTabWidget(editor);
    variables = new QTableWidget(0, 4, tabs);
    variables->setObjectName(QStringLiteral("functionVariables"));
    variables->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Role"), tr("Location")});
    variables->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    variables->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    variables->verticalHeader()->hide();
    variables->setSelectionBehavior(QAbstractItemView::SelectRows);
    variables->setToolTip(
        tr("Edit local variable names and types here. Edit parameters in the signature."));
    tabs->addTab(variables, tr("Variables"));
    information = new QTreeWidget(tabs);
    information->setObjectName(QStringLiteral("functionInformation"));
    information->setHeaderLabels({tr("Property"), tr("Value")});
    information->setRootIsDecorated(false);
    information->setSelectionMode(QAbstractItemView::ExtendedSelection);
    information->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tabs->addTab(information, tr("Analysis information (read-only)"));
    body->addWidget(tabs, 1);
    layout->addWidget(editor, 1);
    status = new QLabel(this);
    status->setObjectName(QStringLiteral("functionDetailsStatus"));
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status);
    buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    auto refreshButton = buttons->addButton(tr("Refresh"), QDialogButtonBox::ResetRole);
    refreshButton->setObjectName(QStringLiteral("refreshFunctionDetails"));
    connect(refreshButton, &QPushButton::clicked, this, &FunctionDetailsDialog::refresh);
    connect(
        buttons->button(QDialogButtonBox::Apply),
        &QPushButton::clicked,
        this,
        &FunctionDetailsDialog::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(nameEdit, &QLineEdit::textChanged, this, &FunctionDetailsDialog::markDirty);
    connect(stackEdit, &QLineEdit::textChanged, this, &FunctionDetailsDialog::markDirty);
    connect(
        callingConvention, &QComboBox::currentTextChanged, this, &FunctionDetailsDialog::markDirty);
    connect(signatureEdit, &QPlainTextEdit::textChanged, this, &FunctionDetailsDialog::markDirty);
    connect(variables, &QTableWidget::itemChanged, this, &FunctionDetailsDialog::markDirty);
    connect(Core(), &IaitoCore::functionsChanged, this, &FunctionDetailsDialog::refreshIfClean);
    connect(Core(), &IaitoCore::functionRenamed, this, &FunctionDetailsDialog::refreshIfClean);
    connect(Core(), &IaitoCore::refreshCodeViews, this, &FunctionDetailsDialog::refreshIfClean);
    connect(Core(), &IaitoCore::refreshAll, this, &FunctionDetailsDialog::refreshIfClean);
    connect(Core(), &IaitoCore::varsChanged, this, &FunctionDetailsDialog::refreshIfClean);
    refresh();
}

FunctionDetailsDialog *FunctionDetailsDialog::showFunction(RVA address, QWidget *parent)
{
    auto dialog = new FunctionDetailsDialog(address, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    return dialog;
}

QJsonObject FunctionDetailsDialog::functionInfo() const
{
    // An exact entry check prevents editing a different function after deletion or reanalysis.
    auto core = Core()->core();
    if (!r_anal_get_function_at(core->anal, functionAddress)) {
        return {};
    }
    const auto functions = Core()->cmdjAt("afij", functionAddress).array();
    return functions.isEmpty() ? QJsonObject() : functions.first().toObject();
}

void FunctionDetailsDialog::refresh()
{
    loading = true;
    originalInfo = functionInfo();
    available = !originalInfo.isEmpty();
    editor->setEnabled(available);
    dirty = false;
    buttons->button(QDialogButtonBox::Apply)->setEnabled(false);
    if (!available) {
        setWindowTitle(tr("Function details — %1").arg(RAddressString(functionAddress)));
        setError(tr("This function no longer exists. Refresh after defining it again."));
        loading = false;
        return;
    }
    const QString name = originalInfo.value("name").toString();
    setWindowTitle(tr("Function details — %1 (%2)").arg(name, RAddressString(functionAddress)));
    nameEdit->setText(name);
    stackEdit->setText(jsonText(originalInfo.value("stackframe")));
    callingConvention->clear();
    callingConvention->addItems(Core()->cmdList("afcl"));
    const QString convention = originalInfo.value("calltype").toString();
    if (callingConvention->findText(convention) < 0) {
        callingConvention->addItem(convention);
    }
    callingConvention->setCurrentText(convention);
    originalSignature = Core()->cmdRawAt("afs", functionAddress).trimmed();
    signatureEdit->setPlainText(originalSignature);
    originalAttributes = originalInfo.value("attrs").toString();
    for (auto it = attributes.cbegin(); it != attributes.cend(); ++it) {
        it.value()->setChecked(originalAttributes.contains(it.key()));
    }

    variables->setRowCount(0);
    const auto vars = Core()->cmdjAt("afvj", functionAddress).object();
    for (const QString &storage :
         {QStringLiteral("reg"), QStringLiteral("bp"), QStringLiteral("sp")}) {
        for (const auto &value : vars.value(storage).toArray()) {
            const auto variable = value.toObject();
            bool local = variable.value("kind").toString() == QStringLiteral("var");
            {
                // afvj labels all register variables "reg", including register locals.
                auto core = Core()->core();
                auto function = r_anal_get_function_at(core->anal, functionAddress);
                auto var = function ? r_anal_function_get_var_byname(
                                          function,
                                          variable.value("name").toString().toUtf8().constData())
                                    : nullptr;
                if (var) {
                    local = !var->isarg;
                }
            }
            const int row = variables->rowCount();
            variables->insertRow(row);
            const auto ref = variable.value("ref");
            QString location = ref.toString();
            if (ref.isObject()) {
                const auto object = ref.toObject();
                const qlonglong offset = object.value("offset").toVariant().toLongLong();
                location = QStringLiteral("%1 %2 %3")
                               .arg(object.value("base").toString(), offset < 0 ? "-" : "+")
                               .arg(
                                   offset < 0 ? QString::number(offset).mid(1)
                                              : QString::number(offset));
            }
            const QStringList values
                = {variable.value("name").toString(),
                   variable.value("type").toString(),
                   local ? tr("Local") : tr("Parameter"),
                   location};
            for (int column = 0; column < values.size(); ++column) {
                auto item = new QTableWidgetItem(values.at(column));
                item->setData(Qt::UserRole, values.at(column));
                if (!local || column >= 2) {
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                }
                variables->setItem(row, column, item);
            }
        }
    }
    information->clear();
    for (auto it = originalInfo.constBegin(); it != originalInfo.constEnd(); ++it) {
        QString value = jsonText(it.value());
        if (it.key() == "addr") {
            value = RAddressString(functionAddress);
        } else if (it.key() == "minaddr" || it.key() == "maxaddr") {
            value = RAddressString(it.value().toVariant().toULongLong());
        }
        // Include every afij field, including fields introduced by newer radare2 versions.
        new QTreeWidgetItem(information, {it.key(), value});
    }
    status->clear();
    loading = false;
}

void FunctionDetailsDialog::markDirty()
{
    if (loading) {
        return;
    }
    dirty = true;
    status->setText(tr("Unapplied changes"));
    buttons->button(QDialogButtonBox::Apply)->setEnabled(available);
}

void FunctionDetailsDialog::refreshIfClean()
{
    if (!loading && !dirty) {
        refresh();
    }
}

void FunctionDetailsDialog::setError(const QString &message)
{
    status->setText(message);
}

void FunctionDetailsDialog::apply()
{
    if (!dirty || !available) {
        return;
    }
    auto core = Core()->core();
    const auto current = functionInfo();
    if (current.isEmpty()) {
        available = false;
        editor->setEnabled(false);
        buttons->button(QDialogButtonBox::Apply)->setEnabled(false);
        setError(tr("This function no longer exists."));
        return;
    }
    const QString name = nameEdit->text().trimmed();
    QString signature = signatureEdit->toPlainText().trimmed();
    const QString oldName = originalInfo.value("name").toString();
    const bool renamed = name != oldName;
    const bool signatureChanged = signature != originalSignature;
    if (renamed) {
        signature.replace(
            QRegularExpression(
                "(?<![\\w.$:])" + QRegularExpression::escape(oldName) + "(?=\\s*\\()"),
            name);
    }
    bool validStack = false;
    const auto stackText = stackEdit->text().trimmed();
    const qlonglong stack
        = stackText
              .toLongLong(&validStack, stackText.startsWith("0x", Qt::CaseInsensitive) ? 16 : 10);
    if (!validName(name)) {
        setError(tr("Enter a valid function name."));
        return;
    }
    if (!validStack || stack < 0 || stack > INT_MAX) {
        setError(tr("Enter a stack frame size between 0 and %1.").arg(INT_MAX));
        return;
    }
    if (auto other = r_anal_get_function_byname(core->anal, name.toUtf8().constData())) {
        if (other->addr != functionAddress) {
            setError(tr("Another function already has this name."));
            return;
        }
    }
    QString parseError;
    QString parsedSignature;
    if (signatureChanged || renamed) {
        signature = signature.simplified();
        if (!signature.endsWith(QLatin1Char(';'))) {
            signature += QLatin1Char(';');
        }
        if (!validDeclaration(signature, name, parseError, &parsedSignature)) {
            setError(
                tr("Enter a valid signature using the function name above. %1").arg(parseError));
            return;
        }
        // afs extracts the name after the last space before '('. Keep pointer-return
        // declarations such as "char *foo()" from being renamed to "_foo" by r2.
        signature.replace(
            QRegularExpression("(?<![\\w.$:])" + QRegularExpression::escape(name) + "(?=\\s*\\()"),
            QLatin1Char(' ') + name);
    }
    const bool stackChanged = stack != originalInfo.value("stackframe").toVariant().toLongLong();
    const bool conventionChanged = callingConvention->currentText()
                                   != originalInfo.value("calltype").toString();
    const auto conflict = [&](const QString &key, bool changed) {
        return changed && current.value(key) != originalInfo.value(key);
    };
    if (conflict("name", renamed) || conflict("stackframe", stackChanged)
        || conflict("calltype", conventionChanged)
        || ((signatureChanged || renamed)
            && Core()->cmdRawAt("afs", functionAddress).trimmed() != originalSignature)) {
        setError(tr("This function was edited elsewhere. Refresh before applying these changes."));
        return;
    }

    struct VariableEdit
    {
        QString before;
        QString name;
        QString type;
        bool typeChanged;
    };
    QList<VariableEdit> edits;
    QSet<QString> names;
    const auto currentVariables = Core()->getVariables(functionAddress);
    for (int row = 0; row < variables->rowCount(); ++row) {
        auto nameItem = variables->item(row, 0);
        auto typeItem = variables->item(row, 1);
        const QString variableName = nameItem->text().trimmed();
        if (!validName(variableName) || names.contains(variableName)) {
            setError(tr("Variable names must be valid and unique."));
            return;
        }
        names.insert(variableName);
        const QString before = nameItem->data(Qt::UserRole).toString();
        const QString type = typeItem->text().trimmed();
        const bool typeChanged = type != typeItem->data(Qt::UserRole).toString();
        if (variableName == before && !typeChanged) {
            continue;
        }
        bool found = false;
        for (const auto &variable : currentVariables) {
            if (variable.name == variableName && variable.name != before) {
                setError(tr("A variable already has the name %1.").arg(variableName));
                return;
            }
            if (variable.name == before) {
                found = variable.type == typeItem->data(Qt::UserRole).toString();
            }
        }
        if (!found) {
            setError(tr("Variables were edited elsewhere. Refresh before applying these changes."));
            return;
        }
        if (typeChanged
            && (type.isEmpty() || type.contains(QRegularExpression("[;{}\\r\\n]"))
                || !validDeclaration(
                    "void __iaito_local(" + type + ");", "__iaito_local", parseError))) {
            setError(tr("Invalid type for %1. %2").arg(variableName, parseError));
            return;
        }
        edits.append({before, variableName, type, typeChanged});
    }

    // Only write changed fields: another floating window may have edited unrelated metadata.
    loading = true;
    if (renamed) {
        Core()->cmdRawAt(QStringLiteral("'afn %1").arg(name), functionAddress);
    }
    if (signatureChanged || renamed) {
        Core()->cmdRawAt(QStringLiteral("'afs %1").arg(signature), functionAddress);
    }
    if (stackChanged) {
        Core()->cmdRawAt(QStringLiteral("'afS %1").arg(stack), functionAddress);
    }
    if (conventionChanged) {
        Core()->cmdRawAt(
            QStringLiteral("'afc %1").arg(callingConvention->currentText()), functionAddress);
    }
    for (const auto &edit : edits) {
        if (edit.typeChanged) {
            Core()->cmdRawAt(
                QStringLiteral("'afvt %1 %2").arg(edit.before, edit.type), functionAddress);
        }
        if (edit.name != edit.before) {
            Core()->cmdRawAt(
                QStringLiteral("'afvn %1 %2").arg(edit.name, edit.before), functionAddress);
        }
    }
    for (auto it = attributes.cbegin(); it != attributes.cend(); ++it) {
        if (it.value()->isEnabled()
            && it.value()->isChecked() != originalAttributes.contains(it.key())) {
            Core()->cmdRawAt(
                QStringLiteral("afia%1 %2").arg(it.value()->isChecked() ? "+" : "-").arg(it.key()),
                functionAddress);
        }
    }
    const auto saved = functionInfo();
    bool success = (!renamed || saved.value("name").toString() == name)
                   && (!stackChanged || saved.value("stackframe").toInt() == stack)
                   && (!conventionChanged
                       || saved.value("calltype").toString() == callingConvention->currentText());
    if (signatureChanged || renamed) {
        QString savedSignature;
        success &= validDeclaration(
                       Core()->cmdRawAt("afs", functionAddress).trimmed(),
                       name,
                       parseError,
                       &savedSignature)
                   && savedSignature == parsedSignature;
    }
    const auto savedVariables = Core()->getVariables(functionAddress);
    for (const auto &edit : edits) {
        bool found = false;
        for (const auto &variable : savedVariables) {
            found |= variable.name == edit.name && variable.type == edit.type;
        }
        success &= found;
    }
    for (auto it = attributes.cbegin(); it != attributes.cend(); ++it) {
        if (it.value()->isEnabled()
            && it.value()->isChecked() != originalAttributes.contains(it.key())) {
            success &= saved.value("attrs").toString().contains(it.key())
                       == it.value()->isChecked();
        }
    }
    refresh();
    loading = true;
    if (renamed) {
        emit Core() -> functionRenamed(functionAddress, name);
    }
    emit Core() -> functionsChanged();
    emit Core() -> varsChanged();
    emit Core() -> refreshCodeViews();
    loading = false;
    if (!success) {
        setError(
            tr("Some changes could not be applied. The fields show the current function values."));
    }
}
