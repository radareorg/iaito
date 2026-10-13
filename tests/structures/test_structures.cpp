#include "core/Iaito.h"
#include "widgets/HexWidget.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class StructuresTest : public QObject
{
    Q_OBJECT
    QTemporaryDir temporary;

private slots:
    void initTestCase()
    {
        QVERIFY(temporary.isValid());
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("structures");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        Core()->initialize(false);
        Core()->cmd("o malloc://64 0x1000");
        Core()->cmd("wx 78563412 @ 0x1000");
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

};

QTEST_MAIN(StructuresTest)
#include "test_structures.moc"
