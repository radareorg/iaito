#include "common/Configuration.h"
#include "common/TypeLayout.h"
#include "core/Iaito.h"
#include "widgets/HexdumpWidget.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

static const char *definitions = R"TYPES(
Pair=struct
struct.Pair=a,b
struct.Pair.a=uint16_t,0,0
struct.Pair.b=uint16_t,4,0
PairAlias=typedef
typedef.PairAlias=struct Pair
Variant=union
union.Variant=word,wide
union.Variant.word=uint32_t,0,0
union.Variant.wide=uint64_t,0,0
Node=struct
struct.Node=value,next
struct.Node.value=uint32_t,0,0
struct.Node.next=struct Node *,8,0
Choice=enum
enum.Choice=YES,NO
enum.Choice.YES=1
enum.Choice.NO=2
Packet=struct
struct.Packet=magic,pair,list,flags,variant,node,signedValue,choice
struct.Packet.magic=uint32_t,0,0
struct.Packet.pair=PairAlias,4,0
struct.Packet.list=struct Pair,16,2
struct.Packet.flags=uint8_t,32,3
struct.Packet.variant=union Variant,40,0
struct.Packet.node=struct Node *,48,0
struct.Packet.signedValue=int32_t,56,0
struct.Packet.choice=enum Choice,60,0
Bad=struct
struct.Bad=self
struct.Bad.self=struct Bad,0,0
AliasCycle=typedef
typedef.AliasCycle=AliasCycle
)TYPES";

class StructuresTest : public QObject
{
    Q_OBJECT
    QTemporaryDir temporary;

    static void inspect(HexdumpWidget &widget, const QString &type, const QString &address)
    {
        auto *tabs = widget.findChild<QTabWidget *>("hexSideTab_2");
        tabs->setCurrentWidget(widget.findChild<QWidget *>("tabStructures"));
        widget.findChild<QComboBox *>("structureTypeComboBox")->setCurrentText(type);
        widget.findChild<QLineEdit *>("structureAddressEdit")->setText(address);
        widget.findChild<QPushButton *>("structureInspectButton")->click();
    }

    static void clearPins()
    {
        TypeLayout layout;
        layout.load(Core()->cmdRaw("tk"), 8);
        const auto links = layout.links();
        for (auto it = links.cbegin(); it != links.cend(); ++it) {
            Core()->cmdRaw(QStringLiteral("tl- 0x%1").arg(it.key(), 0, 16));
        }
        Core()->cmd("Cr-*");
    }

private slots:
    void initTestCase()
    {
        QVERIFY(temporary.isValid());
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("structures");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Core()->cmd("o malloc://1048576 0x1000");
        Core()->cmd("wx 78563412 @ 0x1000");
        Core()->cmd("wx 0100 @ 0x1004");
        Core()->cmd("wx 0200 @ 0x1008");
        Core()->cmd("wx 0300 @ 0x1010");
        Core()->cmd("wx 0400 @ 0x1014");
        Core()->cmd("wx 0500 @ 0x1016");
        Core()->cmd("wx 0600 @ 0x101a");
        Core()->cmd("wx 070809 @ 0x1020");
        Core()->cmd("wx ffffffffffffffff @ 0x1028");
        Core()->cmd("wx 0011000000000000 @ 0x1030");
        Core()->cmd("wx feffffff01000000 @ 0x1038");
        Core()->cmd("wx 07000000 @ 0x1100");
        Core()->cmd("wx 0011000000000000 @ 0x1108");
        for (const QString &line :
             QString::fromLatin1(definitions).split('\n', Qt::SkipEmptyParts)) {
            Core()->cmdRaw(QStringLiteral("tk ") + line);
        }
        Core()->seek(0x1000);
    }

    void init()
    {
        Core()->setConfig("asm.bits", 64);
        Core()->setConfig("cfg.bigendian", false);
        clearPins();
        Core()->seek(0x1000);
    }

    void layoutAndValues()
    {
        TypeLayout layout;
        layout.load(Core()->cmdRaw("tk"), 8);
        QCOMPARE(layout.size("PairAlias"), quint64(6));
        QCOMPARE(layout.fields("PairAlias").at(1).offset, quint64(4));
        QCOMPARE(layout.size("Packet"), quint64(64));
        QCOMPARE(layout.size("Variant"), quint64(8));
        QCOMPARE(layout.fields("Variant").at(1).offset, quint64(0));
        QCOMPARE(layout.size("Node"), quint64(16));
        QCOMPARE(layout.size("Node *"), quint64(8));
        QCOMPARE(layout.size("uint16_t[2][3]"), quint64(12));
        QCOMPARE(layout.size("uint64_t[18446744073709551615]"), quint64(0));
        QCOMPARE(layout.size("Bad"), quint64(0));
        QCOMPARE(layout.size("AliasCycle"), quint64(0));
        QCOMPARE(
            layout.value("uint64_t", QByteArray(8, char(0xff)), false),
            QString("18446744073709551615 (0xffffffffffffffff)"));
        QCOMPARE(
            layout.value("int32_t", QByteArray::fromHex("feffffff"), false),
            QString("-2 (0xfffffffe)"));
        QCOMPARE(layout.value("uint16_t", QByteArray::fromHex("0102"), true), QString("258 (0x102)"));
        QCOMPARE(layout.value("float", QByteArray::fromHex("0000803f"), false), QString("1"));
        QCOMPARE(
            layout.value("Choice", QByteArray::fromHex("01000000"), false),
            QString("1 (YES) (0x1)"));
        layout.load(Core()->cmdRaw("tk"), 4);
        QCOMPARE(layout.size("Node *"), quint64(4));
    }

    void emptyViewportFetch()
    {
        MemoryData data;
        data.fetch(0x1000, 0);
        QVERIFY(data.minIndex() > data.maxIndex());
        data.fetch(0x1000, -1);
        QVERIFY(data.minIndex() > data.maxIndex());
        data.fetch(0x1000, 4);
        QByteArray bytes(4, 0);
        QVERIFY(data.copy(bytes.data(), 0x1000, 4));
        QCOMPARE(bytes.toHex(), QByteArray("78563412"));
    }

    void nestedFieldsSelectionAndRefresh()
    {
        HexdumpWidget widget(nullptr);
        widget.resize(1200, 700);
        widget.show();
        inspect(widget, "Packet", "0x1000");
        auto *tree = widget.findChild<QTreeWidget *>("structureTree");
        QCOMPARE(tree->topLevelItemCount(), 1);
        auto *root = tree->topLevelItem(0);
        QCOMPARE(root->childCount(), 8);
        QCOMPARE(root->text(4), QString("64"));
        QCOMPARE(root->child(0)->text(2), QString("305419896 (0x12345678)"));
        auto *pair = root->child(1);
        QCOMPARE(pair->text(1), QString("PairAlias"));
        QCOMPARE(pair->childCount(), 1); // Lazy placeholder.
        pair->setExpanded(true);
        QCOMPARE(pair->childCount(), 2);
        QCOMPARE(pair->child(1)->text(2), QString("2 (0x2)"));
        QCOMPARE(pair->child(1)->data(3, Qt::UserRole).toULongLong(), qulonglong(0x1008));
        QVERIFY(pair->child(1)->background(0).color().alpha() > 0);
        auto *list = root->child(2);
        list->setExpanded(true);
        QCOMPARE(list->childCount(), 2);
        list->child(1)->setExpanded(true);
        QCOMPARE(list->child(1)->child(1)->text(2), QString("6 (0x6)"));
        QCOMPARE(list->child(1)->child(1)->data(3, Qt::UserRole).toULongLong(), qulonglong(0x101a));
        root->child(3)->setExpanded(true);
        QCOMPARE(root->child(3)->child(2)->text(2), QString("9 (0x9)"));
        root->child(4)->setExpanded(true);
        QCOMPARE(
            root->child(4)->child(1)->text(2), QString("18446744073709551615 (0xffffffffffffffff)"));
        QCOMPARE(root->child(6)->text(2), QString("-2 (0xfffffffe)"));
        tree->setCurrentItem(pair->child(1));
        auto *hex = widget.findChild<HexWidget *>("hexTextView");
        QCOMPARE(hex->getSelection().startAddress, RVA(0x1008));
        QCOMPARE(hex->getSelection().endAddress, RVA(0x1009));
        QCOMPARE(Core()->getOffset(), RVA(0x1008));
        QCOMPARE(tree->topLevelItem(0)->data(3, Qt::UserRole).toULongLong(), qulonglong(0x1000));
        const QString screenshot = qEnvironmentVariable("STRUCTURES_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            QTest::qWait(30);
            QVERIFY(widget.grab().save(screenshot));
        }

        Core()->cmd("wx 0900 @ 0x1008");
        widget.findChild<QPushButton *>("structureRefreshButton")->click();
        root = tree->topLevelItem(0);
        QVERIFY(root->child(1)->isExpanded());
        QCOMPARE(root->child(1)->child(1)->text(2), QString("9 (0x9)"));
        QCOMPARE(tree->currentItem(), root->child(1)->child(1));
        Core()->cmd("wx 0200 @ 0x1008");
        Core()->setConfig("cfg.bigendian", true);
        widget.findChild<QPushButton *>("structureRefreshButton")->click();
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(2), QString("2018915346 (0x78563412)"));

        inspect(widget, "Packet", "not_an_address");
        QCOMPARE(tree->topLevelItemCount(), 0);
        inspect(widget, "Bad", "0x1000");
        QCOMPARE(tree->topLevelItemCount(), 0);
    }

    void pointersAndLargeArrays()
    {
        HexdumpWidget widget(nullptr);
        inspect(widget, "Packet", "0x1000");
        auto *tree = widget.findChild<QTreeWidget *>("structureTree");
        auto *pointer = tree->topLevelItem(0)->child(5);
        pointer->setExpanded(true);
        QCOMPARE(pointer->childCount(), 1);
        auto *node = pointer->child(0);
        QCOMPARE(node->data(3, Qt::UserRole).toULongLong(), qulonglong(0x1100));
        node->setExpanded(true);
        QCOMPARE(node->child(0)->text(2), QString("7 (0x7)"));
        node->child(1)->setExpanded(true);
        QCOMPARE(node->child(1)->child(0)->text(2), QString("Recursive reference"));
        QCOMPARE(node->child(1)->child(0)->childCount(), 0);

        inspect(widget, "uint8_t[100000]", "0x1000");
        auto *root = tree->topLevelItem(0);
        QVERIFY(root->childCount() <= 256);
        auto *last = root;
        while (last->childCount()) {
            last = last->child(last->childCount() - 1);
            last->setExpanded(true);
            QVERIFY(last->childCount() <= 256);
        }
        QCOMPARE(last->data(3, Qt::UserRole).toULongLong(), qulonglong(0x1000 + 99999));
    }

    void typeAndCommandPins()
    {
        Core()->cmd("tl Packet = 0x1000");
        Core()->cmd("Cr 4 px 4 @ 0x1080");
        Core()->setCommandAnnotation(0x1090, 4, "px 4");
        Core()->setCommandAnnotation(0x10a0, 4, "px 4 @ 0x1000; ?v 123");
        QCOMPARE(Core()->getCommandAnnotation(0x10a0), QString("px 4 @ 0x1000; ?v 123"));
        QCOMPARE(Core()->getOffset(), RVA(0x1000)); // Pinning never executes a command or seeks.
        HexdumpWidget widget(nullptr);
        auto *pins = widget.findChild<QTreeWidget *>("structurePinsTree");
        QCOMPARE(pins->topLevelItemCount(), 4);
        auto *tree = widget.findChild<QTreeWidget *>("structureTree");
        auto *run = widget.findChild<QPushButton *>("structureRunButton");
        QVERIFY(!run->isEnabled());
        pins->itemActivated(pins->topLevelItem(0), 0);
        QCOMPARE(Core()->getOffset(), RVA(0x1000));
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QString("Packet"));
        inspect(widget, "PairAlias", "0x1080");
        widget.findChild<QPushButton *>("structurePinButton")->click();
        QVERIFY(Core()->cmdRaw("tl").contains("0x00001080 = PairAlias"));
        widget.findChild<QPushButton *>("structureRefreshButton")->click();
        QTreeWidgetItem *command = nullptr;
        for (int i = 0; i < pins->topLevelItemCount(); ++i) {
            auto *pin = pins->topLevelItem(i);
            if (pin->data(0, Qt::UserRole).toULongLong() == 0x1080
                && pin->data(1, Qt::UserRole).toBool()) {
                command = pin;
            }
        }
        QVERIFY(command);
        QCOMPARE(command->text(1), QString("px 4"));
        pins->setCurrentItem(command);
        QVERIFY(run->isEnabled());
        run->click();
        QVERIFY(
            !widget.findChild<QPlainTextEdit *>("structureCommandOutput")->toPlainText().isEmpty());
        widget.findChild<QPushButton *>("structureUnpinButton")->click();
        QVERIFY(Core()->getCommandAnnotation(0x1080).isEmpty());
        QVERIFY(Core()->cmdRaw("tl").contains("0x00001080 = PairAlias"));

        TypeLayout layout;
        layout.load(QStringLiteral("link.0020000000000001=Packet\n") + definitions, 8);
        QCOMPARE(layout.links().firstKey(), quint64(0x20000000000001));
    }

};

QTEST_MAIN(StructuresTest)
#include "test_structures.moc"
