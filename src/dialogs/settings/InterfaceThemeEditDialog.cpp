#include "InterfaceThemeEditDialog.h"

#include "common/Configuration.h"
#include "widgets/ColorPicker.h"
#include "widgets/ColorThemeListView.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>

InterfaceThemeEditDialog::InterfaceThemeEditDialog(const QString &themeName, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(
        themeName.isEmpty() ? tr("Interface Theme Editor")
                            : tr("Interface Theme Editor - <%1>").arg(themeName));

    m_vars = themeName.isEmpty() ? Configuration::defaultInterfaceThemeVariables()
                                 : Configuration::loadInterfaceThemeVariables(themeName);

    listView = new ColorThemeListView(this);
    listView->setSource(ColorSettingsModel::InterfacePalette);
    listView->colorSettingsModel()->setInterfaceColors(m_vars);
    colorPicker = new ColorPicker(this);

    auto *top = new QHBoxLayout;
    top->addWidget(listView, 1);
    top->addWidget(colorPicker);

    previewTimer = new QTimer(this);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(50);
    connect(previewTimer, &QTimer::timeout, this, [this]() {
        qApp->setStyleSheet(Configuration::buildInterfaceStyleSheet(m_vars));
        Config()->setColor("navbarCode", m_vars.value("navbarCode"));
        Config()->setColor("navbarString", m_vars.value("navbarString"));
        Config()->setColor("navbarSymbol", m_vars.value("navbarSymbol"));
        Config()->setColor("background", m_vars.value("background"));
        emit Config()->colorsUpdated();
    });

    nameEdit = new QLineEdit(this);
    nameEdit->setPlaceholderText(tr("Theme name"));
    nameEdit->setText(themeName);
    auto *nameRow = new QHBoxLayout;
    nameRow->addWidget(new QLabel(tr("Save as:"), this));
    nameRow->addWidget(nameEdit, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(nameRow);
    layout->addWidget(buttons);

    connect(listView, &ColorThemeListView::itemChanged, colorPicker, &ColorPicker::updateColor);
    connect(
        colorPicker,
        &ColorPicker::colorChanged,
        this,
        &InterfaceThemeEditDialog::colorOptionChanged);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    listView->setCurrentIndex(listView->model()->index(0, 0));
}

void InterfaceThemeEditDialog::colorOptionChanged(const QColor &newColor)
{
    const QModelIndex idx = listView->currentIndex();
    if (!idx.isValid()) {
        return;
    }

    ColorOption opt = idx.data(Qt::UserRole).value<ColorOption>();
    opt.color = newColor;
    opt.changed = true;
    listView->model()->setData(idx, QVariant::fromValue(opt));

    m_vars[opt.optionName] = newColor;
    previewTimer->start();
}

void InterfaceThemeEditDialog::accept()
{
    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Interface Theme Editor"), tr("Please enter a theme name."));
        return;
    }
    if (!Configuration::isValidThemeName(name)) {
        QMessageBox::warning(this, tr("Interface Theme Editor"), tr("Invalid theme name."));
        return;
    }

    Configuration::saveInterfaceThemeVariables(name, m_vars);
    m_savedName = name;
    QDialog::accept();
}

void InterfaceThemeEditDialog::reject()
{
    Config()->setInterfaceTheme(Config()->getInterfaceTheme());
    QDialog::reject();
}
