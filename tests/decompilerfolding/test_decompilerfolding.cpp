#include "common/Configuration.h"
#include "common/Decompiler.h"
#include "common/IaitoSeekable.h"
#include "core/MainWindow.h"
#include "widgets/DecompilerTextEdit.h"
#include "widgets/DecompilerWidget.h"

#include <QLabel>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>

static const QString source = QStringLiteral(
    "void f() {\n"
    "    if (x) {\n"
    "        for (;;) {\n"
    "            work();\n"
    "        }\n"
    "    } else {\n"
    "        fallback();\n"
    "    }\n"
    "    switch (x) {\n"
    "    case 1: {\n"
    "        work();\n"
    "    }\n"
    "    }\n"
    "}\n");

class FoldingDecompiler : public Decompiler
{
public:
    FoldingDecompiler()
        : Decompiler("test-folding", "Folding test")
    {}

    RCodeMeta *decompileSync(RVA) override
    {
        RCodeMeta *code = r_codemeta_new(source.toUtf8().constData());
        int start = 0;
        int line = 0;
        for (const QString &text : source.split(QLatin1Char('\n'))) {
            auto *offset = r_codemeta_item_new();
            offset->type = R_CODEMETA_TYPE_OFFSET;
            offset->start = start;
            offset->end = start + text.size();
            offset->offset.offset = line == 3 ? 4 : 0;
            r_codemeta_add_item(code, offset);
            start += text.size() + 1;
            ++line;
        }
        return code;
    }
    void decompileAt(RVA address) override { emit finished(decompileSync(address)); }
};

class DecompilerFoldingTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("decompilerfolding");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setDecompilerAutoRefreshEnabled(false);
        QVERIFY(Core()->registerDecompiler(new FoldingDecompiler));
        Config()->setSelectedDecompiler("test-folding");
        Core()->cmd("o malloc://64");
        Core()->setConfig("asm.arch", "x86");
        Core()->setConfig("asm.bits", 64);
        Core()->cmd("wx 554889e5b82a0000005dc3 @ 0");
        Core()->cmd("ompg +x");
        Core()->cmd("af @ 0");
    }

    void init()
    {
        Config()->setDecompilerShowAddresses(false);
        Config()->setDecompilerShowDisassembly(false);
        Core()->seek(0);
    }

    void braceBlocks_data()
    {
        QTest::addColumn<QString>("header");
        for (const QString &header :
             {"if (x)", "else", "for (;;)", "while (x)", "switch (x)", "case 1:", "do", ""}) {
            QTest::newRow(header.toUtf8().constData()) << header;
        }
    }

    void braceBlocks()
    {
        QFETCH(QString, header);
        const QString text = header + " {\n    body();\n}\n";
        DecompilerTextEdit edit;
        edit.setDecompiledCode(text, text, false);
        const int position = edit.document()->findBlockByNumber(1).position();
        QVERIFY(edit.isFoldable(0));
        edit.toggleFold(0);
        QVERIFY(edit.isFolded(0));
        QVERIFY(!edit.document()->findBlockByNumber(1).isVisible());
        QVERIFY(edit.document()->findBlockByNumber(2).isVisible());
        QCOMPARE(edit.toPlainText(), text);
        QCOMPARE(edit.document()->findBlockByNumber(1).position(), position);
        edit.toggleFold(0);
        QVERIFY(!edit.isFolded(0));
        QVERIFY(edit.document()->findBlockByNumber(1).isVisible());
    }

    void nestedAndElseBlocks()
    {
        DecompilerTextEdit edit;
        edit.setDecompiledCode(source, source, false);
        edit.toggleFold(2);
        edit.toggleFold(1);
        QVERIFY(!edit.document()->findBlockByNumber(2).isVisible());
        QVERIFY(edit.document()->findBlockByNumber(5).isVisible());
        QVERIFY(edit.isFoldable(5));
        edit.toggleFold(1);
        QVERIFY(edit.document()->findBlockByNumber(2).isVisible());
        QVERIFY(!edit.document()->findBlockByNumber(3).isVisible());
        edit.toggleFold(5);
        QVERIFY(!edit.document()->findBlockByNumber(6).isVisible());
        QVERIFY(edit.document()->findBlockByNumber(7).isVisible());
        edit.expandAll();
        for (auto block = edit.document()->begin(); block.isValid(); block = block.next()) {
            QVERIFY(block.isVisible());
        }
        edit.collapseAll();
        QVERIFY(edit.isFolded(0));
        QVERIFY(!edit.document()->findBlockByNumber(1).isVisible());
        QCOMPARE(edit.toPlainText(), source);
    }

    void ignoreLiteralsAndComments()
    {
        const QString text = QStringLiteral(
            "void f() {\n"
            "    puts(\"{\\\"}\");\n"
            "    char c = '}';\n"
            "    // { ignored\n"
            "    /* {\n"
            "       } */\n"
            "    auto s = R\"tag({\n"
            "        ignored();\n"
            "    })tag\";\n"
            "    if (x) {\n"
            "        work();\n"
            "    }\n"
            "}\n");
        DecompilerTextEdit edit;
        edit.setDecompiledCode(text, text, false);
        for (int line = 0; line < edit.blockCount(); ++line) {
            QCOMPARE(edit.isFoldable(line), line == 0 || line == 9);
        }
        const QString incomplete = "}\nif (x) {\n    work();\n";
        edit.setDecompiledCode(incomplete, incomplete, false);
        QVERIFY(!edit.isFoldable(0));
        QVERIFY(!edit.isFoldable(1));
    }

    void cursorRevealsEnclosingFolds()
    {
        DecompilerTextEdit edit;
        edit.setDecompiledCode(source, source, false);
        edit.collapseAll();
        QTextCursor cursor(edit.document()->findBlockByNumber(3));
        edit.setTextCursor(cursor);
        QVERIFY(edit.document()->findBlockByNumber(3).isVisible());
        QVERIFY(!edit.isFolded(0));
        QVERIFY(!edit.isFolded(1));
        QVERIFY(!edit.isFolded(2));
        QVERIFY(edit.isFolded(5));
        QVERIFY(edit.isFolded(8));
    }

    void bracesSharingAnOpeningLine()
    {
        const QString text = "if (a) { if (b) {\n    work();\n} }\n";
        DecompilerTextEdit edit;
        edit.setDecompiledCode(text, text, false);
        edit.collapseAll();
        QVERIFY(!edit.document()->findBlockByNumber(1).isVisible());
        edit.toggleFold(0);
        QVERIFY(edit.document()->findBlockByNumber(1).isVisible());
        QVERIFY(!edit.isFolded(0));
    }

    void gutterAndScrolling()
    {
        DecompilerTextEdit edit;
        edit.resize(500, 250);
        const QString text = "void f() {\n" + QString("    work();\n").repeated(100) + "}\n";
        edit.setDecompiledCode(text, text, false);
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));
        auto *area = edit.findChild<QWidget *>("decompilerFoldArea");
        QVERIFY(area && area->isVisible());
        QTRY_VERIFY(edit.verticalScrollBar()->maximum() > 0);
        const int maximum = edit.verticalScrollBar()->maximum();
        QTextCursor cursor(edit.document()->findBlockByNumber(1));
        edit.setTextCursor(cursor);
        QSignalSpy seeks(&edit, &QPlainTextEdit::cursorPositionChanged);
        const int y = edit.cursorRect(QTextCursor(edit.document()->firstBlock())).center().y();
        const QPoint point(
            area->width() / 2, edit.viewport()->mapTo(&edit, QPoint(0, y)).y() - area->pos().y());
        QTest::mouseMove(area, point);
        QCOMPARE(area->cursor().shape(), Qt::PointingHandCursor);
        QTest::mouseClick(area, Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(edit.isFolded(0));
        QCOMPARE(edit.textCursor().blockNumber(), 0);
        QCOMPARE(seeks.count(), 0);
        QTRY_VERIFY(edit.verticalScrollBar()->maximum() < maximum);
        QTest::mouseClick(area, Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(!edit.isFolded(0));
        QTRY_COMPARE(edit.verticalScrollBar()->maximum(), maximum);
    }

    void preserveFoldsAcrossColumnsAndRefresh()
    {
        MainWindow main;
        DecompilerWidget widget(&main);
        widget.getSeekable()->setSynchronization(false);
        widget.setParent(nullptr);
        widget.resize(1000, 600);
        widget.show();
        QTRY_VERIFY(widget.isVisibleToUser());
        QVERIFY(QMetaObject::invokeMethod(&widget, "refreshDecompiler"));
        auto *edit = widget.findChild<DecompilerTextEdit *>("textEdit");
        QVERIFY(edit);
        QTRY_COMPARE(edit->toPlainText(), source);
        QTextCursor cursor(edit->document()->findBlockByNumber(3));
        edit->setTextCursor(cursor);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(4));
        edit->toggleFold(2);
        edit->toggleFold(1);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(4));
        for (bool addresses : {true, false}) {
            widget.setAddressColumnVisible(addresses);
            widget.setDisassemblyColumnVisible(addresses);
            QVERIFY(edit->isFolded(1));
            QVERIFY(edit->isFolded(2));
            QVERIFY(!edit->document()->findBlockByNumber(3).isVisible());
            QVERIFY(edit->textCursor().block().isVisible());
        }
        QVERIFY(QMetaObject::invokeMethod(&widget, "refreshDecompiler"));
        auto *progress = widget.findChild<QLabel *>("progressLabel");
        QVERIFY(progress);
        QTRY_VERIFY(!progress->isVisible());
        QVERIFY(edit->isFolded(1));
        QVERIFY(edit->isFolded(2));
        widget.getSeekable()->seek(0);
        widget.getSeekable()->seek(4);
        QVERIFY(!edit->isFolded(1));
        QVERIFY(!edit->isFolded(2));
        QCOMPARE(edit->textCursor().blockNumber(), 3);
        QVERIFY(edit->textCursor().block().isVisible());
        QCOMPARE(Core()->getOffset(), RVA(0));
    }
};

QTEST_MAIN(DecompilerFoldingTests)
#include "test_decompilerfolding.moc"
