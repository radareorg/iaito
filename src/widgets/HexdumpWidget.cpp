#include "HexdumpWidget.h"
#include "ui_HexdumpWidget.h"

#include "common/Configuration.h"
#include "common/Helpers.h"
#include "common/SyntaxHighlighter.h"
#include "common/TempConfig.h"
#include "core/MainWindow.h"

#include <QClipboard>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QScrollBar>
#include <QShortcut>
#include <QTextDocumentFragment>

HexdumpWidget::HexdumpWidget(MainWindow *main)
    : MemoryDockWidget(MemoryWidgetType::Hexdump, main)
    , ui(new Ui::HexdumpWidget)
{
    ui->setupUi(this);

    setObjectName(main ? main->getUniqueObjectName(getWidgetType()) : getWidgetType());
    updateWindowTitle();

    ui->copyMD5->setIcon(QIcon(":/img/icons/copy.svg"));
    ui->copySHA1->setIcon(QIcon(":/img/icons/copy.svg"));
    ui->copySHA256->setIcon(QIcon(":/img/icons/copy.svg"));
    ui->copyCRC32->setIcon(QIcon(":/img/icons/copy.svg"));

    ui->splitter->setChildrenCollapsible(false);

    QToolButton *closeButton = new QToolButton;
    QIcon closeIcon = QIcon(":/img/icons/delete.svg");
    closeButton->setIcon(closeIcon);
    closeButton->setAutoRaise(true);

    ui->hexSideTab_2->setCornerWidget(closeButton);
    syntaxHighLighter = Config()->createSyntaxHighlighter(ui->hexDisasTextEdit->document());

    ui->openSideViewB->hide(); // hide button at startup since side view is visible

    connect(closeButton, &QToolButton::clicked, this, [this] { showSidePanel(false); });

    connect(ui->openSideViewB, &QToolButton::clicked, this, [this] { showSidePanel(true); });

    // Set placeholders for the line-edit components
    QString placeholder = tr("Select bytes to display information");
    ui->bytesMD5->setPlaceholderText(placeholder);
    ui->bytesEntropy->setPlaceholderText(placeholder);
    ui->bytesSHA1->setPlaceholderText(placeholder);
    ui->bytesSHA256->setPlaceholderText(placeholder);
    ui->bytesCRC32->setPlaceholderText(placeholder);
    ui->hexDisasTextEdit->setPlaceholderText(placeholder);

    setupFonts();

    ui->openSideViewB->setStyleSheet(""
                                     "QToolButton {"
                                     "   border : 0px;"
                                     "   padding : 0px;"
                                     "   margin : 0px;"
                                     "}"
                                     "QToolButton:hover {"
                                     "  border : 1px solid;"
                                     "  border-width : 1px;"
                                     "  border-color : #3daee9"
                                     "}");

    refreshDeferrer = createReplacingRefreshDeferrer<RVA>(false, [this](const RVA *offset) {
        refresh(offset ? *offset : RVA_INVALID);
    });

    this->ui->hexTextView->addAction(&syncAction);

    connect(Config(), &Configuration::fontsUpdated, this, &HexdumpWidget::fontsUpdated);
    connect(Core(), &IaitoCore::refreshAll, this, [this]() { refresh(); });
    connect(Core(), &IaitoCore::refreshCodeViews, this, [this]() { refresh(); });
    connect(Core(), &IaitoCore::instructionChanged, this, [this]() { refresh(); });
    connect(Core(), &IaitoCore::stackChanged, this, [this]() { refresh(); });
    connect(Core(), &IaitoCore::registersChanged, this, [this]() { refresh(); });

    connect(seekable, &IaitoSeekable::seekableSeekChanged, this, &HexdumpWidget::onSeekChanged);
    connect(ui->hexTextView, &HexWidget::positionChanged, this, [this](RVA addr) {
        if (!sent_seek) {
            sent_seek = true;
            seekable->seek(addr);
            sent_seek = false;
        }
    });
    connect(ui->hexTextView, &HexWidget::selectionChanged, this, &HexdumpWidget::selectionChanged);
    connect(ui->hexSideTab_2, &QTabWidget::currentChanged, this, &HexdumpWidget::refreshSelectionInfo);
    ui->hexTextView->installEventFilter(this);

    initParsing();
    selectHexPreview();

    // apply initial offset
    refresh(seekable->getOffset());
}

void HexdumpWidget::onSeekChanged(RVA addr)
{
    if (sent_seek) {
        sent_seek = false;
        return;
    }
    refresh(addr);
}

HexdumpWidget::~HexdumpWidget() {}

QString HexdumpWidget::getWidgetType()
{
    return "Hexdump";
}

void HexdumpWidget::refresh()
{
    refresh(RVA_INVALID);
}

void HexdumpWidget::refresh(RVA addr)
{
    if (!refreshDeferrer->attemptRefresh(addr == RVA_INVALID ? nullptr : new RVA(addr))) {
        return;
    }
    sent_seek = true;
    if (addr != RVA_INVALID) {
        ui->hexTextView->seek(addr);
    } else {
        ui->hexTextView->refresh();
        refreshSelectionInfo();
    }
    sent_seek = false;
}

void HexdumpWidget::initParsing()
{
    // Fill the plugins combo for the hexdump sidebar
    ui->parseTypeComboBox->addItem(tr("Disassembly"), "pda");
    ui->parseTypeComboBox->addItem(tr("String"), "pcs");
    ui->parseTypeComboBox->addItem(tr("Assembler"), "pca");
    ui->parseTypeComboBox->addItem(tr("C bytes"), "pc");
    ui->parseTypeComboBox->addItem(tr("C bytes with instructions"), "pci");
    ui->parseTypeComboBox->addItem(tr("C half-words (2 byte)"), "pch");
    ui->parseTypeComboBox->addItem(tr("C words (4 byte)"), "pcw");
    ui->parseTypeComboBox->addItem(tr("C dwords (8 byte)"), "pcd");
    ui->parseTypeComboBox->addItem(tr("Python"), "pcp");
    ui->parseTypeComboBox->addItem(tr("JSON"), "pcj");
    ui->parseTypeComboBox->addItem(tr("JavaScript"), "pcJ");
    ui->parseTypeComboBox->addItem(tr("Yara"), "pcy");

    ui->parseArchComboBox->insertItems(0, Core()->getAsmPluginNames());

    ui->parseEndianComboBox->setCurrentIndex(Core()->getConfigb("cfg.bigendian") ? 1 : 0);
}

void HexdumpWidget::selectionChanged(HexWidget::Selection selection)
{
    if (selection.empty) {
        clearParseWindow();
    } else {
        updateParseWindow(selection.startAddress, selection.endAddress - selection.startAddress + 1);
    }
}

void HexdumpWidget::on_parseArchComboBox_currentTextChanged(const QString & /*arg1*/)
{
    refreshSelectionInfo();
}

void HexdumpWidget::on_parseBitsComboBox_currentTextChanged(const QString & /*arg1*/)
{
    refreshSelectionInfo();
}

void HexdumpWidget::setupFonts()
{
    QFont font = Config()->getFont();
    ui->hexDisasTextEdit->setFont(font);
    ui->hexTextView->setMonospaceFont(font);
}

void HexdumpWidget::refreshSelectionInfo()
{
    selectionChanged(ui->hexTextView->getSelection());
}

void HexdumpWidget::fontsUpdated()
{
    setupFonts();
}

void HexdumpWidget::clearParseWindow()
{
    ui->hexDisasTextEdit->setPlainText("");
    ui->bytesEntropy->setText("");
    ui->bytesMD5->setText("");
    ui->bytesSHA1->setText("");
    ui->bytesSHA256->setText("");
    ui->bytesCRC32->setText("");
}

void HexdumpWidget::showSidePanel(bool show)
{
    ui->hexSideTab_2->setVisible(show);
    ui->openSideViewB->setHidden(show);
    if (show) {
        refreshSelectionInfo();
    }
}

QString HexdumpWidget::getWindowTitle() const
{
    return tr("Hexdump");
}

void HexdumpWidget::updateParseWindow(RVA start_address, int size)
{
    if (!ui->hexSideTab_2->isVisible()) {
        return;
    }

    if (ui->hexSideTab_2->currentIndex() == 0) {
        // scope for TempConfig

        // Get selected combos
        QString arch = ui->parseArchComboBox->currentText();
        QString bits = ui->parseBitsComboBox->currentText();
        QString selectedCommand = ui->parseTypeComboBox->currentData().toString();
        QString commandResult = "";
        bool bigEndian = ui->parseEndianComboBox->currentIndex() == 1;

        TempConfig tempConfig;
        tempConfig.set("asm.arch", arch).set("asm.bits", bits).set("cfg.bigendian", bigEndian);

        ui->hexDisasTextEdit->setPlainText(
            selectedCommand != ""
                ? Core()->cmdRawAt(QStringLiteral("%1 %2").arg(selectedCommand).arg(size), start_address)
                : "");
    } else {
        // Fill the information tab hashes and entropy
        ui->bytesMD5->setText(
            Core()->cmdRawAt(QStringLiteral("ph md5 %1").arg(size), start_address).trimmed());
        ui->bytesSHA1->setText(
            Core()->cmdRawAt(QStringLiteral("ph sha1 %1").arg(size), start_address).trimmed());
        ui->bytesSHA256->setText(
            Core()->cmdRawAt(QStringLiteral("ph sha256 %1").arg(size), start_address).trimmed());
        ui->bytesCRC32->setText(
            Core()->cmdRawAt(QStringLiteral("ph crc32 %1").arg(size), start_address).trimmed());
        ui->bytesEntropy->setText(
            Core()->cmdRawAt(QStringLiteral("ph entropy %1").arg(size), start_address).trimmed());
        ui->bytesMD5->setCursorPosition(0);
        ui->bytesSHA1->setCursorPosition(0);
        ui->bytesSHA256->setCursorPosition(0);
        ui->bytesCRC32->setCursorPosition(0);
    }
}

void HexdumpWidget::on_parseTypeComboBox_currentTextChanged(const QString &)
{
    QString currentParseTypeText = ui->parseTypeComboBox->currentData().toString();
    if (currentParseTypeText == "pda" || currentParseTypeText == "pci") {
        ui->hexSideFrame_2->show();
    } else {
        ui->hexSideFrame_2->hide();
    }
    refreshSelectionInfo();
}

void HexdumpWidget::on_parseEndianComboBox_currentTextChanged(const QString &)
{
    refreshSelectionInfo();
}

void HexdumpWidget::on_hexSideTab_2_currentChanged(int /*index*/)
{
    /*
    if (index == 2) {
        // Add data to HTML Polar functions graph
        QFile html(":/html/bar.html");
        if(!html.open(QIODevice::ReadOnly)) {
            QMessageBox::information(0,"error",html.errorString());
        }
        QString code = html.readAll();
        html.close();
        this->histoWebView->setHtml(code);
        this->histoWebView->show();
    } else {
        this->histoWebView->hide();
    }
    */
}

void HexdumpWidget::resizeEvent(QResizeEvent *event)
{
    // Heuristics to hide sidebar when it hides the content of the hexdump.
    // 600px looks just "okay" Only applied when widget width is decreased to
    // avoid unwanted behavior
    if (event->oldSize().width() > event->size().width() && event->size().width() < 600) {
        showSidePanel(false);
    }
    QDockWidget::resizeEvent(event);
    refresh();
}

QWidget *HexdumpWidget::widgetToFocusOnRaise()
{
    return ui->hexTextView;
}

void HexdumpWidget::on_copyMD5_clicked()
{
    QString md5 = ui->bytesMD5->text();
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(md5);
    Core()->message("MD5 copied to clipboard: " + md5);
}

void HexdumpWidget::on_copySHA1_clicked()
{
    QString sha1 = ui->bytesSHA1->text();
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(sha1);
    Core()->message("SHA1 copied to clipboard: " + sha1);
}

void HexdumpWidget::on_copySHA256_clicked()
{
    QString sha256 = ui->bytesSHA256->text();
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(sha256);
    Core()->message("SHA256 copied to clipboard: " + sha256);
}

void HexdumpWidget::on_copyCRC32_clicked()
{
    QString crc32 = ui->bytesCRC32->text();
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(crc32);
    Core()->message("CRC32 copied to clipboard: " + crc32);
}

void HexdumpWidget::selectHexPreview()
{
    // Pre-select arch and bits in the hexdump sidebar
    QString arch = Core()->getConfig("asm.arch");
    QString bits = Core()->getConfig("asm.bits");

    if (ui->parseArchComboBox->findText(arch) != -1) {
        ui->parseArchComboBox->setCurrentIndex(ui->parseArchComboBox->findText(arch));
    }

    if (ui->parseBitsComboBox->findText(bits) != -1) {
        ui->parseBitsComboBox->setCurrentIndex(ui->parseBitsComboBox->findText(bits));
    }
}
