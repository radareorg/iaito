#include "Dashboard.h"
#include "common/Helpers.h"
#include "common/JsonModel.h"
#include "common/JsonTreeItem.h"
#include "common/TempConfig.h"
#include "dialogs/VersionInfoDialog.h"
#include "ui_Dashboard.h"

#include "IaitoTreeView.h"
#include "core/MainWindow.h"

#include <QDebug>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayoutItem>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QTreeWidget>

Dashboard::Dashboard(MainWindow *main)
    : IaitoDockWidget(main)
    , ui(new Ui::Dashboard)
{
    ui->setupUi(this);

    connect(Core(), &IaitoCore::refreshAll, this, &Dashboard::updateContents);
}

Dashboard::~Dashboard() {}

void Dashboard::updateContents()
{
    QJsonDocument docu = Core()->getFileInfo();
    QJsonObject item = docu.object()["core"].toObject();
    QJsonObject item2 = docu.object()["bin"].toObject();

    setPlainText(this->ui->fileEdit, item["file"].toString());
    setPlainText(this->ui->formatEdit, item["format"].toString());
    setPlainText(this->ui->modeEdit, item["mode"].toString());
    setPlainText(this->ui->typeEdit, item["type"].toString());
    setPlainText(this->ui->sizeEdit, qhelpers::formatBytecount(item["size"].toDouble()));
    setPlainText(this->ui->fdEdit, QString::number(item["fd"].toDouble()));

    setPlainText(this->ui->archEdit, item2["arch"].toString());
    setPlainText(this->ui->langEdit, item2["lang"].toString().toUpper());
    setPlainText(this->ui->classEdit, item2["class"].toString());
    setPlainText(this->ui->machineEdit, item2["machine"].toString());
    setPlainText(this->ui->osEdit, item2["os"].toString());
    setPlainText(this->ui->subsysEdit, item2["subsys"].toString());
    setPlainText(this->ui->endianEdit, item2["endian"].toString());
    setPlainText(this->ui->compilationDateEdit, item2["compiled"].toString());
    setPlainText(this->ui->compilerEdit, item2["compiler"].toString());
    setPlainText(this->ui->bitsEdit, QString::number(item2["bits"].toDouble()));

    if (!item2["relro"].toString().isEmpty()) {
        QString relro = item2["relro"].toString().section(QLatin1Char(' '), 0, 0);
        relro[0] = relro[0].toUpper();
        setPlainText(this->ui->relroEdit, relro);
    } else {
        setPlainText(this->ui->relroEdit, "N/A");
    }

    setPlainText(this->ui->baddrEdit, RAddressString(item2["baddr"].toVariant().toULongLong()));

    // set booleans
    setBool(this->ui->vaEdit, item2, "va");
    setBool(this->ui->canaryEdit, item2, "canary");
    setBool(this->ui->cryptoEdit, item2, "crypto");
    setBool(this->ui->nxEdit, item2, "nx");
    setBool(this->ui->picEdit, item2, "pic");
    setBool(this->ui->staticEdit, item2, "static");
    setBool(this->ui->strippedEdit, item2, "stripped");
    setBool(this->ui->relocsEdit, item2, "relocs");

    // Add file hashes, analysis info and libraries

    QJsonObject hashes = Core()->cmdj("itj").object();

    // Delete hashesWidget if it isn't null to avoid duplicate components
    if (hashesWidget) {
        hashesWidget->deleteLater();
    }

    // Define dynamic components to hold the hashes
    hashesWidget = new QWidget();
    QFormLayout *hashesLayout = new QFormLayout;
    hashesWidget->setLayout(hashesLayout);
    ui->hashesVerticalLayout->addWidget(hashesWidget);

    // Add hashes as a pair of Hash Name : Hash Value.
    for (const QString &key : hashes.keys()) {
        // Create a bold QString with the hash name uppercased
        QString label = QStringLiteral("<b>%1:</b>").arg(key.toUpper());

        // Define a Read-Only line edit to display the hash value
        QLineEdit *hashLineEdit = new QLineEdit();
        hashLineEdit->setReadOnly(true);
        hashLineEdit->setText(hashes.value(key).toString());

        // Set cursor position to begining to avoid long hashes (e.g sha256)
        // to look truncated at the begining
        hashLineEdit->setCursorPosition(0);

        // Add both controls to a form layout in a single row
        hashesLayout->addRow(new QLabel(label), hashLineEdit);
    }

    // Add the Entropy value of the file to the dashboard
    {
        // Scope for TempConfig
        TempConfig tempConfig;
        tempConfig.set("io.va", false);

        // Calculate the Entropy of the entire binary from offset 0 to $s
        // where $s is the size of the entire file
        QString entropy = Core()->cmdRawAt("ph entropy $s", 0).trimmed();

        // Define a Read-Only line edit to display the entropy value
        QLineEdit *entropyLineEdit = new QLineEdit();
        entropyLineEdit->setReadOnly(true);
        entropyLineEdit->setText(entropy);
        hashesLayout->addRow(new QLabel(tr("<b>Entropy:</b>")), entropyLineEdit);
    }

    QJsonObject analinfo = Core()->cmdj("aaij").object();
    setPlainText(ui->functionsLineEdit, QString::number(analinfo["fcns"].toInt()));
    setPlainText(ui->xRefsLineEdit, QString::number(analinfo["xrefs"].toInt()));
    setPlainText(ui->callsLineEdit, QString::number(analinfo["calls"].toInt()));
    setPlainText(ui->stringsLineEdit, QString::number(analinfo["strings"].toInt()));
    setPlainText(ui->symbolsLineEdit, QString::number(analinfo["symbols"].toInt()));
    setPlainText(ui->importsLineEdit, QString::number(analinfo["imports"].toInt()));
    setPlainText(ui->coverageLineEdit, QString::number(analinfo["covrage"].toInt()) + " bytes");
    setPlainText(ui->codeSizeLineEdit, QString::number(analinfo["codesz"].toInt()) + " bytes");
    setPlainText(ui->percentageLineEdit, QString::number(analinfo["percent"].toInt()) + "%");

    QStringList libs = Core()->cmdList("il");
    if (!libs.isEmpty()) {
        libs.removeFirst();
        libs.removeLast();
    }

    // dunno: why not label->setText(lines.join("\n")?
    while (ui->verticalLayout_2->count() > 0) {
        QLayoutItem *item = ui->verticalLayout_2->takeAt(0);
        if (item != nullptr) {
            QWidget *w = item->widget();
            if (w != nullptr) {
                w->deleteLater();
            }

            delete item;
        }
    }

    for (const QString &lib : libs) {
        QLabel *label = new QLabel(this);
        label->setText(lib);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->verticalLayout_2->addWidget(label);
    }

    QSpacerItem *spacer = new QSpacerItem(1, 1, QSizePolicy::Fixed, QSizePolicy::Expanding);
    ui->verticalLayout_2->addSpacerItem(spacer);

    // Get stats for the graphs
    QStringList stats = Core()->getStats();

    // Check if signature info and version info available
    if (Core()->getSignatureInfo().isEmpty()) {
        ui->certificateButton->setEnabled(false);
    }
    if (Core()->getFileVersionInfo().isEmpty()) {
        ui->versioninfoButton->setEnabled(false);
    }
}

void Dashboard::on_certificateButton_clicked()
{
    static QDialog *viewDialog = nullptr;
    static IaitoTreeView *view = nullptr;
    static JsonModel *model = nullptr;
    static QString qstrCertificates;
    if (!viewDialog) {
        viewDialog = new QDialog(this);
        view = new IaitoTreeView(viewDialog);
        model = new JsonModel();
        QJsonDocument qjsonCertificatesDoc = Core()->getSignatureInfo();
        qstrCertificates = qjsonCertificatesDoc.toJson(QJsonDocument::Compact);
    }
    if (!viewDialog->isVisible()) {
        std::string strCertificates = qstrCertificates.toUtf8().constData();
        model->loadJson(QByteArray::fromStdString(strCertificates));
        view->setModel(model);
        view->expandAll();
        view->resize(900, 600);
        QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(view->sizePolicy().hasHeightForWidth());
        viewDialog->setSizePolicy(sizePolicy);
        viewDialog->setMinimumSize(QSize(900, 600));
        viewDialog->setMaximumSize(QSize(900, 600));
        viewDialog->setSizeGripEnabled(false);
        viewDialog->setWindowTitle("Certificates");
        viewDialog->show();
    }
}

void Dashboard::on_versioninfoButton_clicked()
{
    static QDialog *infoDialog = nullptr;

    if (!infoDialog) {
        infoDialog = new VersionInfoDialog(this);
    }

    if (!infoDialog->isVisible()) {
        infoDialog->show();
    }
}

/**
 * @brief Set the text of a QLineEdit. If no text, then "N/A" is set.
 * @param textBox
 * @param text
 */
void Dashboard::setPlainText(QLineEdit *textBox, const QString &text)
{
    if (!text.isEmpty()) {
        textBox->setText(text);
    } else {
        textBox->setText(tr("N/A"));
    }

    textBox->setCursorPosition(0);
}

/**
 * @brief Set the text of a QLineEdit as True, False or N/A if it does not exist
 * @param textBox
 * @param isTrue
 */
void Dashboard::setBool(QLineEdit *textBox, const QJsonObject &jsonObject, const QString &key)
{
    if (jsonObject.contains(key)) {
        if (jsonObject[key].toBool()) {
            setPlainText(textBox, tr("True"));
        } else {
            setPlainText(textBox, tr("False"));
        }
    } else {
        setPlainText(textBox, tr("N/A"));
    }
}
