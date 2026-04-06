#include "AboutDialog.h"
#include "common/Radare2Compat.h"
#include "core/Iaito.h"

#include "PackageManagerDialog.h"
#include "R2PluginsDialog.h"
#include "common/Configuration.h"
#include "ui_AboutDialog.h"

#include <QJsonObject>

#include "IaitoConfig.h"

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AboutDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
    ui->logoSvgWidget->load(Config()->getLogoFile());

    QString aboutString(
        tr("Version") + " " IAITO_VERSION_FULL "<br/>" + tr("Using r2-") + R2_GITTAP + "<br/>"
        + buildQtVersionString() + "<h2>" + tr("License") + "</h2>"
        + tr(
            "This Software is released under the GNU General "
            "Public License v3.0")
        + "<h2>" + tr("Authors") + "</h2>"
        + tr(
            "Iaito is developed by the community and maintained "
            "by its core and development teams.<br/>")
        + tr(
            "Check our <a "
            "href='https://github.com/radareorg/iaito/graphs/"
            "contributors'>contributors page</a> for the full "
            "list of contributors. Or go to <a "
            "href='https://www.radare.org/'>www.radare.org</a> "
            "for more details."));
    ui->label->setText(aboutString);
}

AboutDialog::~AboutDialog() {}

void AboutDialog::on_showPackageManagerButton_clicked()
{
    PackageManagerDialog dlg(this);
    dlg.exec();
}

void AboutDialog::on_buttonBox_rejected()
{
    close();
}

void AboutDialog::on_showVersionButton_clicked()
{
    QMessageBox popup(this);
    popup.setWindowTitle(tr("radare2 version information"));
    popup.setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto versionInformation = Core()->getVersionInformation();
    popup.setText(versionInformation);
    popup.exec();
}

void AboutDialog::on_showPluginsButton_clicked()
{
    R2PluginsDialog dialog(this);
    dialog.exec();
}

static QString compilerString()
{
#if defined(Q_CC_GNU)
    return QLatin1String("GCC ") + QLatin1String(__VERSION__);
#elif defined(Q_CC_CLANG)
    QString isAppleString;
#if defined(__apple_build_version__) // Apple clang has other version numbers
    isAppleString = QLatin1String(" (Apple)");
#endif
    return QLatin1String("Clang ") + QString::number(__clang_major__) + QLatin1Char('.')
           + QString::number(__clang_minor__) + isAppleString;
#elif defined(Q_CC_MSVC)
    if (_MSC_VER > 1999)
        return QLatin1String("MSVC <unknown>");
    if (_MSC_VER >= 1910)
        return QLatin1String("MSVC 2017");
    if (_MSC_VER >= 1900)
        return QLatin1String("MSVC 2015");
#endif
    return QLatin1String("<unknown compiler>");
}

QString AboutDialog::buildQtVersionString(void)
{
    return tr("Based on Qt %1 (%2, %3 bit)")
        .arg(QLatin1String(qVersion()), compilerString(), QString::number(QSysInfo::WordSize));
}
