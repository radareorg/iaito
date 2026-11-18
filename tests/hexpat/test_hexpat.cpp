#include "core/Iaito.h"
#include "widgets/HexPatWidget.h"

#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

class HexPatTest : public QObject
{
    Q_OBJECT
    QTemporaryDir temporary;

    void writeFile(const QString &name, const QByteArray &contents)
    {
        QFile file(temporary.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(temporary.isValid());
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("hexpat");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Core()->cmd("o malloc://64 0x1000");
        Core()->cmd("wx 7856341201000200ffffffffffffffff21 @ 0x1000");
        Core()->cmd("wx 180000003412 @ 0x1014");
    }

    void availabilityAndNonBinaryPatterns()
    {
        HexPatWidget widget(nullptr);
        auto *reload = widget.findChild<QPushButton *>("reloadButton");
        auto *tree = widget.findChild<QTreeWidget *>("patternsTree");
        QVERIFY(!widget.toggleViewAction()->isEnabled());
        QVERIFY(!reload->isEnabled());

        // Exercise heap/section handling with the same JSON interface as hexpatj.
        writeFile("fixture.r2.js", R"JS(
r2.plugin("core", function () {
    return { name: "hexpat", desc: "widget test", license: "MIT", call: function (cmd) {
        if (!cmd.startsWith("hexpatj ")) return false;
        r2.log(JSON.stringify([
            { name: "local", kind: "unsigned", type: "u8", addr: 0x1001,
              offset: 1, size: 1, local: true, value: 42 },
            { name: "section", kind: "unsigned", type: "u8", addr: 0x1002,
              offset: 2, size: 1, section: 1, value: 43 },
            { name: "formatted", kind: "unsigned", type: "u8", addr: 0x1003,
              offset: 3, size: 1, value: 44, repr: "named value" }
        ]));
        return true;
    }};
});
)JS");
        Core()->cmd(". " + temporary.filePath("fixture.r2.js"));
        widget.updateAvailability();
        QVERIFY(widget.toggleViewAction()->isEnabled());
        QVERIFY(reload->isEnabled());
        widget.findChild<QLineEdit *>("fileEdit")->setText(temporary.filePath("fixture.r2.js"));
        reload->click();
        QCOMPARE(tree->topLevelItemCount(), 3);
        QVERIFY(!tree->topLevelItem(0)->data(2, Qt::UserRole).isValid());
        QVERIFY(!tree->topLevelItem(1)->data(2, Qt::UserRole).isValid());
        QCOMPARE(tree->topLevelItem(2)->text(4), QString("named value"));
        QSignalSpy seek(Core(), &IaitoCore::seekChanged);
        tree->itemActivated(tree->topLevelItem(0), 0);
        tree->itemActivated(tree->topLevelItem(1), 0);
        QCOMPARE(seek.count(), 0);
        tree->itemActivated(tree->topLevelItem(2), 0);
        QCOMPARE(Core()->getOffset(), RVA(0x1003));

        writeFile("unload.r2.js", "r2.unload('core', 'hexpat');");
        Core()->cmd(". " + temporary.filePath("unload.r2.js"));
        widget.updateAvailability();
        QVERIFY(!widget.toggleViewAction()->isEnabled());
        QVERIFY(!reload->isEnabled());
        QCOMPARE(tree->topLevelItemCount(), 0);
    }

    void realPlugin()
    {
        const QString plugin = qEnvironmentVariable("HEXPAT_PLUGIN");
        if (plugin.isEmpty()) {
            QSKIP("Set HEXPAT_PLUGIN to hexpat.r2.js to run the integration test.");
        }
        QVERIFY(QFileInfo(plugin).isFile());
        Core()->cmd(". " + QFileInfo(plugin).absoluteFilePath());
        HexPatWidget widget(nullptr);
        QVERIFY(widget.toggleViewAction()->isEnabled());
        auto *path = widget.findChild<QLineEdit *>("fileEdit");
        auto *reload = widget.findChild<QPushButton *>("reloadButton");
        auto *tree = widget.findChild<QTreeWidget *>("patternsTree");
        auto *filter = widget.findChild<QLineEdit *>("filterEdit");
        auto *status = widget.findChild<QLabel *>("statusLabel");
        writeFile("header.pat", "struct Header { u32 magic; u16 flags[2]; };\n");
        writeFile("test.hexpat", R"PAT(
#pragma endian little
#pragma base_address 0x1000
#include "header.pat"
Header header @ 0;
u64 wide @ 8;
bitfield Flags { low: 4; high: 4; };
Flags flags @ 16;
u16 *pointer : u32 @ 20;
)PAT");
        path->setText(temporary.filePath("test.hexpat"));
        reload->click();
        QCOMPARE(tree->topLevelItemCount(), 4);
        auto *header = tree->topLevelItem(0);
        QCOMPARE(header->text(0), QString("header"));
        QCOMPARE(header->text(1), QString("Header"));
        QCOMPARE(header->data(2, Qt::UserRole).toULongLong(), qulonglong(0x1000));
        QCOMPARE(header->childCount(), 2);
        QCOMPARE(header->child(0)->text(4), QString("305419896"));
        QCOMPARE(header->child(1)->childCount(), 2);
        QCOMPARE(header->child(1)->child(1)->text(0), QString("[1]"));
        QCOMPARE(header->child(1)->child(1)->text(4), QString("2"));
        QCOMPARE(tree->topLevelItem(1)->text(4), QString("18446744073709551615"));
        QCOMPARE(tree->topLevelItem(2)->child(0)->text(3), QString("4 bits"));
        QCOMPARE(tree->topLevelItem(2)->child(0)->text(4), QString("1"));
        QCOMPARE(tree->topLevelItem(3)->childCount(), 1);
        QCOMPARE(
            tree->topLevelItem(3)->child(0)->data(2, Qt::UserRole).toULongLong(),
            qulonglong(0x1018));

        filter->setText("magic");
        QVERIFY(!header->isHidden());
        QVERIFY(!header->child(0)->isHidden());
        QVERIFY(header->child(1)->isHidden());
        QVERIFY(tree->topLevelItem(1)->isHidden());
        filter->setText("Header");
        QVERIFY(!header->child(1)->child(1)->isHidden());
        filter->clear();
        QVERIFY(!tree->topLevelItem(1)->isHidden());
        tree->itemActivated(header->child(1)->child(1), 0);
        QCOMPARE(Core()->getOffset(), RVA(0x1006));

        Core()->cmd("wx 0900 @ 0x1006");
        reload->click();
        QCOMPARE(tree->topLevelItem(0)->child(1)->child(1)->text(4), QString("9"));

        // Errors and empty results must not retain fields from the previous evaluation.
        writeFile("test.hexpat", "This is not a pattern;");
        reload->click();
        QCOMPARE(tree->topLevelItemCount(), 0);
        QVERIFY(status->text().contains("failed"));
        writeFile("test.hexpat", "// No placed patterns.\n");
        reload->click();
        QCOMPARE(tree->topLevelItemCount(), 0);
        QVERIFY(status->text().contains("without placed patterns"));

        // An extra include search directory is passed through to the plugin.
        QVERIFY(QDir(temporary.path()).mkdir("includes"));
        writeFile("includes/extra.pat", "struct Extra { u8 byte; };\n");
        writeFile("test.hexpat", "#include <extra.pat>\nExtra extra @ 0;\n");
        widget.findChild<QLineEdit *>("includeEdit")->setText(temporary.filePath("includes"));
        reload->click();
        QCOMPARE(tree->topLevelItemCount(), 1);

        // The current plugin cannot quote paths: reject them before command dispatch.
        for (const QString &name : {QString("with space.hexpat"), QString("bad;name.hexpat")}) {
            writeFile(name, "u8 byte @ 0;");
            path->setText(temporary.filePath(name));
            reload->click();
            QCOMPARE(tree->topLevelItemCount(), 0);
            QVERIFY(status->text().contains("metacharacters"));
        }
        path->setText(temporary.filePath("missing.hexpat"));
        reload->click();
        QVERIFY(status->text().contains("readable"));
    }
};

QTEST_MAIN(HexPatTest)
#include "test_hexpat.moc"
