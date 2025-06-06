#include "HexdumpWidget.h"
#include "ui_HexdumpWidget.h"

#include "common/Configuration.h"
#include "common/Helpers.h"
#include "common/SyntaxHighlighter.h"
#include "common/TempConfig.h"
#include "core/MainWindow.h"

#include <cstring>
#include <QByteArray>
#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QStatusBar>
#include <QStringList>
#include <QTextDocumentFragment>
#include <QTextOption>
#include <QtEndian>

HexdumpWidget::HexdumpWidget(MainWindow *main)
    : MemoryDockWidget(MemoryWidgetType::Hexdump, main)
    , ui(new Ui::HexdumpWidget)
{
    ui->setupUi(this);
    // use character-level wrapping in art (histogram) tab
    ui->histogram->setWordWrapMode(QTextOption::WrapAnywhere);
    // Connect hex view cursor movement to status bar message
    connect(ui->hexTextView, &HexWidget::positionChanged, this, [this](uint64_t addr) {
        QString text = RAddressString(addr);
        QString func = Core()->cmdFunctionAt(addr);
        if (!func.isEmpty()) {
            text += QStringLiteral(" ") + func;
        }
        // Display address and function in the main window's status bar
        if (mainWindow) {
            mainWindow->statusBar()->showMessage(text);
        }
    });
    // Layout for Art tab: combobox and histogram
    ui->comboBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->histogram->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout *artLayout = new QVBoxLayout(ui->tabHistogram);
    artLayout->setContentsMargins(0, 0, 0, 0);
    artLayout->addWidget(ui->comboBox);
    artLayout->addWidget(ui->histogram);
    // Update Art tab when the selection in the combobox changes
    connect(
        ui->comboBox,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &HexdumpWidget::refreshSelectionInfo);

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
    // Write values back to memory when editing is finished
    connect(ui->v_int8, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_int8, 1, true);
    });
    connect(ui->v_uint8, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_uint8, 1, false);
    });
    connect(ui->v_int16, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_int16, 2, true);
    });
    connect(ui->v_uint16, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_uint16, 2, false);
    });
    connect(ui->v_int24, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_int24, 3, true);
    });
    connect(ui->v_uint24, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_uint24, 3, false);
    });
    connect(ui->v_int32, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_int32, 4, true);
    });
    connect(ui->v_uint32, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_uint32, 4, false);
    });
    connect(ui->v_int48, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_int48, 6, true);
    });
    connect(ui->v_uint48, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_uint48, 6, false);
    });
    connect(ui->v_int64, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_int64, 8, true);
    });
    connect(ui->v_uint64, &QLineEdit::editingFinished, this, [this] {
        writeValueEdit(ui->v_uint64, 8, false);
    });

    // ui->histogram->setLayout(ui->tabHistogram->layout());

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
        this->current_address = addr;
    });
    // Also update parse/value pane when cursor moves without selection
    connect(ui->hexTextView, &HexWidget::positionChanged, this, &HexdumpWidget::refreshSelectionInfo);
    connect(ui->hexTextView, &HexWidget::selectionChanged, this, &HexdumpWidget::selectionChanged);
    connect(ui->hexSideTab_2, &QTabWidget::currentChanged, this, &HexdumpWidget::refreshSelectionInfo);
    ui->hexTextView->installEventFilter(this);
    // Monitor side tab resize to update value display
    ui->hexSideTab_2->installEventFilter(this);

    initParsing();
    // Populate Format tab struct selector and inline format
    {
        // Load type names from radare2
        QString typesOutput = Core()->cmdRaw("pf.~[0]"); // ts");
        QStringList typesList = typesOutput.split('\n', Qt::SkipEmptyParts);
        ui->formatStructComboBox->addItem(QString());
        for (const QString &typeName : typesList) {
            ui->formatStructComboBox->addItem(typeName);
        }
        ui->formatInlineEdit->setPlaceholderText(tr("Enter struct or format spec"));
        connect(
            ui->formatStructComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &HexdumpWidget::refreshSelectionInfo);
        connect(
            ui->formatInlineEdit,
            &QLineEdit::editingFinished,
            this,
            &HexdumpWidget::refreshSelectionInfo);
    }
    selectHexPreview();

    // apply initial offset
    refresh(seekable->getOffset());
    // add vim-like hjkl navigation in hexdump: map to arrow keys and Shift for selection
    {
        auto make_nav = [&](Qt::Key key, int arrowKey, Qt::KeyboardModifiers mod) {
            QShortcut *sc = new QShortcut(QKeySequence(mod | key), this);
            sc->setContext(Qt::WidgetWithChildrenShortcut);
            connect(sc, &QShortcut::activated, this, [this, arrowKey, mod]() {
                QKeyEvent ke(QEvent::KeyPress, arrowKey, mod);
                QCoreApplication::sendEvent(ui->hexTextView, &ke);
            });
        };
        // plain movement: h/j/k/l -> left/down/up/right
        make_nav(Qt::Key_H, Qt::Key_Left, Qt::NoModifier);
        make_nav(Qt::Key_J, Qt::Key_Down, Qt::NoModifier);
        make_nav(Qt::Key_K, Qt::Key_Up, Qt::NoModifier);
        make_nav(Qt::Key_L, Qt::Key_Right, Qt::NoModifier);
        // selection: H/J/K/L -> Shift+arrows
        make_nav(Qt::Key_H, Qt::Key_Left, Qt::ShiftModifier);
        make_nav(Qt::Key_J, Qt::Key_Down, Qt::ShiftModifier);
        make_nav(Qt::Key_K, Qt::Key_Up, Qt::ShiftModifier);
        make_nav(Qt::Key_L, Qt::Key_Right, Qt::ShiftModifier);
    }
}

// Event filter to catch resize events on the side tabs and refresh values
bool HexdumpWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->hexSideTab_2 && event->type() == QEvent::Resize) {
        refreshSelectionInfo();
    }
    return MemoryDockWidget::eventFilter(watched, event);
}

// Helper to write edited value fields back to memory
void HexdumpWidget::writeValueEdit(QLineEdit *editor, int byteCount, bool isSigned)
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    bool ok = false;
    quint64 uValue = 0;
    if (isSigned) {
        qint64 signedValue = editor->text().toLongLong(&ok);
        if (!ok) {
            return;
        }
        if (byteCount < 8) {
            quint64 mask = (1ULL << (byteCount * 8)) - 1;
            uValue = static_cast<quint64>(signedValue) & mask;
        } else {
            uValue = static_cast<quint64>(signedValue);
        }
    } else {
        uValue = editor->text().toULongLong(&ok);
        if (!ok) {
            return;
        }
        if (byteCount < 8) {
            quint64 mask = (1ULL << (byteCount * 8)) - 1;
            uValue &= mask;
        }
    }
    // Format as hex with fixed width
    QString hex = QString::number(uValue, 16).rightJustified(byteCount * 2, QLatin1Char('0'));
    // Handle endianness: little endian reverses byte order
    bool bigEndian = ui->valueEndian->currentIndex() == 1;
    if (!bigEndian) {
        QString rev;
        for (int i = hex.length(); i > 0; i -= 2) {
            rev += hex.mid(i - 2, 2);
        }
        hex = rev;
    }
    Core()->editBytes(this->current_address, hex);
}

void HexdumpWidget::onSeekChanged(RVA addr)
{
    this->current_address = addr;
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
    // this->current_address = ui->hexTextView->cursor.address;
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
        updateParseWindow(selection.startAddress, 0);
    } else {
        updateParseWindow(selection.startAddress, selection.endAddress); //  - selection.startAddress + 1);
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
    ui->formatOutput->setFont(font);
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
#if 0
    ui->v_int8->setText("");
    ui->v_uint8->setText("");
    ui->v_int16->setText("");
    ui->v_uint16->setText("");
    ui->v_int24->setText("");
    ui->v_uint24->setText("");
    ui->v_int32->setText("");
    ui->v_uint32->setText("");
    ui->v_int48->setText("");
    ui->v_uint48->setText("");
    ui->v_int64->setText("");
    ui->v_uint64->setText("");
    ui->v_uint64->setText("");
#endif
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

void HexdumpWidget::updateParseWindow(RVA start_address, RVA end_address)
{
    int size = end_address - start_address - 1;
    if (!ui->hexSideTab_2->isVisible()) {
        return;
    }
    uint64_t at = (size > 0 && this->current_address > start_address) ? this->current_address - 1
                                                                      : this->current_address;
    auto cmd = [start_address](const QString &c, uint64_t addr = (uint64_t) -1) {
        return Core()->cmdRawAt(c, (addr == (uint64_t) -1) ? start_address : addr).trimmed();
    };

    const int selectedTab = ui->hexSideTab_2->currentIndex();
    switch (selectedTab) {
    case 0: // disasm
        if (size > 0) {
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
                    ? Core()->cmdRawAt(
                          QStringLiteral("%1 %2").arg(selectedCommand).arg(size), start_address)
                    : "");
        }
        break;
    case 1: // hash
        if (size > 0) {
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
        break;
    case 2: // values
    {
        // Display interpreted values from memory in various types
        bool bigEndian = ui->valueEndian->currentIndex() == 1;
        Core()->setConfig("cfg.bigendian", bigEndian);
        // Read up to 8 bytes from memory at the selected address
        QByteArray buf = Core()->ioRead(at, 8);
        // Pad buffer if not enough bytes
        while ((int) buf.size() < 8) {
            buf.append(char(0));
        }
        const uchar *data = reinterpret_cast<const uchar *>(buf.constData());
        // int8 / uint8
        qint8 v8s = *reinterpret_cast<const qint8 *>(data);
        quint8 v8u = data[0];
        // int16 / uint16
        quint16 raw16 = bigEndian ? qFromBigEndian<quint16>(data)
                                  : qFromLittleEndian<quint16>(data);
        qint16 v16s = *reinterpret_cast<const qint16 *>(&raw16);
        quint16 v16u = raw16;
        // int24 / uint24
        quint32 raw24 = bigEndian
                            ? (quint32(data[0]) << 16 | quint32(data[1]) << 8 | quint32(data[2]))
                            : (quint32(data[2]) << 16 | quint32(data[1]) << 8 | quint32(data[0]));
        qint32 v24s = (raw24 & 0x800000) ? qint32(raw24 | 0xFF000000) : qint32(raw24);
        quint32 v24u = raw24;
        // int32 / uint32
        quint32 raw32 = bigEndian ? qFromBigEndian<quint32>(data)
                                  : qFromLittleEndian<quint32>(data);
        qint32 v32s = *reinterpret_cast<const qint32 *>(&raw32);
        quint32 v32u = raw32;
        // int48 / uint48
        quint64 raw48 = 0;
        if (bigEndian) {
            for (int i = 0; i < 6; i++) {
                raw48 = (raw48 << 8) | quint64(data[i]);
            }
        } else {
            for (int i = 0; i < 6; i++) {
                raw48 |= quint64(data[i]) << (i * 8);
            }
        }
        qint64 v48s = (raw48 & (quint64(1) << 47)) ? qint64(raw48 | 0xFFFF000000000000ULL)
                                                   : qint64(raw48);
        quint64 v48u = raw48;
        // int64 / uint64
        quint64 raw64 = bigEndian ? qFromBigEndian<quint64>(data)
                                  : qFromLittleEndian<quint64>(data);
        qint64 v64s = *reinterpret_cast<const qint64 *>(&raw64);
        quint64 v64u = raw64;
        // float32
        quint32 rawf32 = bigEndian ? qFromBigEndian<quint32>(data)
                                   : qFromLittleEndian<quint32>(data);
        float vf32;
        memcpy(&vf32, &rawf32, sizeof(vf32));
        // double (float64)
        quint64 rawf64 = bigEndian ? qFromBigEndian<quint64>(data)
                                   : qFromLittleEndian<quint64>(data);
        double vf64;
        memcpy(&vf64, &rawf64, sizeof(vf64));
        // Update UI fields
        ui->v_int8->setText(QString::number(v8s));
        ui->v_uint8->setText(QString::number(v8u));
        ui->v_int16->setText(QString::number(v16s));
        ui->v_uint16->setText(QString::number(v16u));
        ui->v_int24->setText(QString::number(v24s));
        ui->v_uint24->setText(QString::number(v24u));
        ui->v_int32->setText(QString::number(v32s));
        ui->v_uint32->setText(QString::number(v32u));
        ui->v_int48->setText(QString::number(v48s));
        ui->v_uint48->setText(QString::number(v48u));
        ui->v_int64->setText(QString::number(v64s));
        ui->v_uint64->setText(QString::number(v64u));
        ui->v_float32->setText(QString::number(vf32, 'g', 8));
        ui->v_double->setText(QString::number(vf64, 'g', 16));
    } break;
    case 3: // Format
    {
        // Print formatted data using struct or inline spec
        QString structName = ui->formatStructComboBox->currentText().trimmed();
        QString inlineFmt = ui->formatInlineEdit->text().trimmed();
        QString out;
        if (!structName.isEmpty()) {
            out = Core()->cmdRawAt(QStringLiteral("%1").arg(structName), start_address).trimmed();
        } else if (!inlineFmt.isEmpty()) {
            if (inlineFmt.startsWith(".")) {
                out = Core()
                          ->cmdRawAt(QStringLiteral("pf%1").arg(inlineFmt), start_address)
                          .trimmed();
            } else {
                out = Core()
                          ->cmdRawAt(QStringLiteral("pf %1").arg(inlineFmt), start_address)
                          .trimmed();
            }
        }
        ui->formatOutput->setPlainText(out);
    } break;
    case 4: // Art (histogram/graph)
    {
        const int w = ui->histogram->width() / 12;
        // Build command based on combobox selection
        QString cmdBase = ui->comboBox->currentText().trimmed();
#if R2_VERSION_NUMBER >= 50909
        char *s = r_str_newf("%s 4096@e:hex.addr=0@e:hex.cols=%d", qPrintable(cmdBase), w);
#else
        char *s = r_str_newf("%s 4096@e:hex.offset=0@e:hex.cols=%d", qPrintable(cmdBase), w);
#endif
        ui->histogram->setText(cmd(s, at));
        free(s);
    } break;
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

// React to changes in the value-endianness combo: update config and refresh displayed values
void HexdumpWidget::on_valueEndian_currentIndexChanged(int index)
{
    Core()->setConfig("cfg.bigendian", index == 1);
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
