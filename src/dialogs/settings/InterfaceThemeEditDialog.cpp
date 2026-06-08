#include "InterfaceThemeEditDialog.h"

#include "common/Configuration.h"
#include "common/theme/IaitoStyle.h"
#include "widgets/ColorPicker.h"
#include "widgets/ColorThemeListView.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace {

struct MetricField
{
    const char *label;
    int ThemeMetrics::*field;
    int min;
    int max;
};

const MetricField kEditorMetrics[] = {
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Corner radius"), &ThemeMetrics::borderRadius, 0, 24},
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Checkbox radius"), &ThemeMetrics::checkBoxRadius, 0, 16},
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Border width"), &ThemeMetrics::borderWidth, 0, 4},
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Focus width"), &ThemeMetrics::focusWidth, 0, 6},
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Spacing"), &ThemeMetrics::spacing, 0, 24},
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Menu padding"), &ThemeMetrics::menuItemPadding, 0, 24},
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Control height"), &ThemeMetrics::controlHeight, 16, 48},
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Checkbox size"), &ThemeMetrics::checkBoxSize, 10, 28},
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Icon size"), &ThemeMetrics::iconSize, 8, 32},
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Scrollbar thickness"), &ThemeMetrics::scrollBarThickness, 6, 24},
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Slider groove"), &ThemeMetrics::sliderGrooveHeight, 2, 16},
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Tab border"), &ThemeMetrics::tabBorderWidth, 0, 4},
    {QT_TRANSLATE_NOOP("InterfaceThemeEditDialog", "Tab indicator"), &ThemeMetrics::tabIndicatorWidth, 0, 8},
};
const int kEditorMetricCount = int(sizeof(kEditorMetrics) / sizeof(kEditorMetrics[0]));

Theme loadSourceTheme(const QString &name)
{
    if (!name.isEmpty() && Configuration::isCustomInterfaceTheme(name)) {
        return Theme::load(
            QDir(Configuration::userThemesDir()).filePath(name + QStringLiteral(".theme")),
            Configuration::interfaceThemeIsDark(name));
    }
    if (!name.isEmpty() && name != QStringLiteral("Native")) {
        return Theme::builtin(name);
    }
    if (IaitoStyle::instance()) {
        return IaitoStyle::instance()->theme();
    }
    return Theme::builtin(QStringLiteral("Dark"));
}

} // namespace

InterfaceThemeEditDialog::InterfaceThemeEditDialog(const QString &themeName, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(
        themeName.isEmpty() ? tr("Interface Theme Editor")
                            : tr("Interface Theme Editor - <%1>").arg(themeName));

    m_base = loadSourceTheme(themeName);
    m_vars = m_base.toChrome();
    m_dark = m_base.dark;

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
        Config()->loadEngineTheme(buildTheme());
        emit Config() -> colorsUpdated();
    });

    skinCombo = new QComboBox(this);
    skinCombo->addItem(tr("Modern (flat)"));
    skinCombo->addItem(tr("Bevel (3D)"));
    skinCombo->setCurrentIndex(m_base.skin == Skin::Bevel ? 1 : 0);
    chromeCombo = new QComboBox(this);
    chromeCombo->addItem(tr("Flat"));
    chromeCombo->addItem(tr("Accent"));
    chromeCombo->setCurrentIndex(m_base.chromeStyle == ChromeStyle::Accent ? 1 : 0);
    connect(skinCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        previewTimer->start();
    });
    connect(chromeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        previewTimer->start();
    });
    auto *styleRow = new QHBoxLayout;
    styleRow->addWidget(new QLabel(tr("Skin:"), this));
    styleRow->addWidget(skinCombo);
    styleRow->addSpacing(12);
    styleRow->addWidget(new QLabel(tr("Chrome:"), this));
    styleRow->addWidget(chromeCombo);
    styleRow->addStretch(1);

    auto *metricsBox = new QGroupBox(tr("Metrics"), this);
    auto *grid = new QGridLayout(metricsBox);
    for (int i = 0; i < kEditorMetricCount; ++i) {
        const MetricField &m = kEditorMetrics[i];
        auto *spin = new QSpinBox(this);
        spin->setRange(m.min, m.max);
        spin->setSuffix(tr(" px"));
        spin->setValue(m_base.metrics.*(m.field));
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            previewTimer->start();
        });
        metricSpins.append(spin);
        const int row = i / 2;
        const int col = (i % 2) * 2;
        grid->addWidget(new QLabel(tr(m.label), this), row, col);
        grid->addWidget(spin, row, col + 1);
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);

    nameEdit = new QLineEdit(this);
    nameEdit->setPlaceholderText(tr("Theme name"));
    nameEdit->setText(themeName);
    modeCombo = new QComboBox(this);
    modeCombo->addItem(tr("Dark"));
    modeCombo->addItem(tr("Light"));
    modeCombo->setCurrentIndex(m_dark ? 0 : 1);
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_dark = (idx == 0);
        previewTimer->start();
    });
    auto *nameRow = new QHBoxLayout;
    nameRow->addWidget(new QLabel(tr("Save as:"), this));
    nameRow->addWidget(nameEdit, 1);
    nameRow->addWidget(new QLabel(tr("Mode:"), this));
    nameRow->addWidget(modeCombo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(styleRow);
    layout->addWidget(metricsBox);
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

Theme InterfaceThemeEditDialog::buildTheme() const
{
    Theme t = Theme::fromChrome(m_vars, m_dark);
    t.skin = skinCombo->currentIndex() == 1 ? Skin::Bevel : Skin::Modern;
    t.chromeStyle = chromeCombo->currentIndex() == 1 ? ChromeStyle::Accent : ChromeStyle::Flat;
    for (int i = 0; i < metricSpins.size(); ++i) {
        t.metrics.*(kEditorMetrics[i].field) = metricSpins[i]->value();
    }
    t.fonts = m_base.fonts;
    t.bevelHighlight = m_base.bevelHighlight;
    t.bevelLight = m_base.bevelLight;
    t.bevelDark = m_base.bevelDark;
    t.bevelShadow = m_base.bevelShadow;
    t.tooltipBase = m_base.tooltipBase;
    t.tooltipText = m_base.tooltipText;
    t.name = nameEdit->text().trimmed();
    t.dark = m_dark;
    return t;
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

    Theme t = buildTheme();
    t.save(QDir(Configuration::userThemesDir()).filePath(name + QStringLiteral(".theme")));
    m_savedName = name;
    QDialog::accept();
}

void InterfaceThemeEditDialog::reject()
{
    Config()->setInterfaceTheme(Config()->getInterfaceTheme());
    QDialog::reject();
}
