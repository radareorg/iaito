#include "common/Configuration.h"
#include "common/IaitoSeekable.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class SeekableHistoryTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("seekablehistory");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Core()->cmd("o malloc://16384");
    }

    void init()
    {
        Core()->seek(0x100);
        auto core = Core()->core();
        r_io_sundo_reset(core->io);
    }

    void detachAtCurrentAddress()
    {
        IaitoSeekable view;
        Core()->seek(0x200);
        view.setSynchronization(false);
        QCOMPARE(view.getOffset(), RVA(0x200));
        QVERIFY(!view.canSeekPrev());
        QVERIFY(!view.canSeekNext());
        view.seekPrev();
        view.seekNext();
        view.seek(RVA_INVALID);
        QCOMPARE(view.getOffset(), RVA(0x200));
    }

    void backForwardAndBranch()
    {
        IaitoSeekable view;
        view.setSynchronization(false);
        view.seek(0x200);
        view.seek(0x300);
        QVERIFY(view.canSeekPrev());
        QVERIFY(!view.canSeekNext());
        view.seekPrev();
        QCOMPARE(view.getOffset(), RVA(0x200));
        view.seekPrev();
        QCOMPARE(view.getOffset(), RVA(0x100));
        QVERIFY(!view.canSeekPrev());
        view.seekPrev();
        QCOMPARE(view.getOffset(), RVA(0x100));
        view.seekNext();
        QCOMPARE(view.getOffset(), RVA(0x200));
        view.seekNext();
        QCOMPARE(view.getOffset(), RVA(0x300));
        view.seekNext();
        QCOMPARE(view.getOffset(), RVA(0x300));
        view.seekPrev();
        view.seek(0x400);
        QVERIFY(!view.canSeekNext());
        view.seekPrev();
        QCOMPARE(view.getOffset(), RVA(0x200));
        view.seekNext();
        QCOMPARE(view.getOffset(), RVA(0x400));
        QCOMPARE(Core()->getOffset(), RVA(0x100));
    }

    void repeatedSeekKeepsForwardHistory()
    {
        IaitoSeekable view;
        view.setSynchronization(false);
        view.seek(0x200);
        view.seekPrev();
        QSignalSpy seeks(&view, &IaitoSeekable::seekableSeekChanged);
        view.seek(0x100);
        view.seek(RVA_INVALID);
        QCOMPARE(seeks.count(), 0);
        QVERIFY(view.canSeekNext());
        view.seekNext();
        QCOMPARE(view.getOffset(), RVA(0x200));
        view.seekPrev();
        QVERIFY(!view.canSeekPrev());
    }

    void independentViewsAndGlobalHistory()
    {
        IaitoSeekable first;
        IaitoSeekable second;
        first.setSynchronization(false);
        second.setSynchronization(false);
        first.seek(0x200);
        second.seek(0x300);
        Core()->seek(0x400);
        first.seekPrev();
        QCOMPARE(first.getOffset(), RVA(0x100));
        QCOMPARE(second.getOffset(), RVA(0x300));
        QCOMPARE(Core()->getOffset(), RVA(0x400));
        Core()->seekPrev();
        QCOMPARE(Core()->getOffset(), RVA(0x100));
        QCOMPARE(second.getOffset(), RVA(0x300));
        first.seekNext();
        QCOMPARE(first.getOffset(), RVA(0x200));
    }

    void reattachAndDetach()
    {
        IaitoSeekable view;
        view.setSynchronization(false);
        view.seek(0x200);
        Core()->seek(0x300);
        view.setSynchronization(true);
        QCOMPARE(view.getOffset(), RVA(0x300));
        view.seekPrev();
        QCOMPARE(Core()->getOffset(), RVA(0x100));
        view.seekNext();
        QCOMPARE(Core()->getOffset(), RVA(0x300));
        view.setSynchronization(false);
        QCOMPARE(view.getOffset(), RVA(0x300));
        QVERIFY(!view.canSeekPrev());
        view.seek(0x400);
        view.setSynchronization(false);
        view.seekPrev();
        QCOMPARE(view.getOffset(), RVA(0x300));
        QVERIFY(!view.canSeekPrev());
    }

    void historyAvailabilitySignals()
    {
        IaitoSeekable view;
        view.setSynchronization(false);
        QSignalSpy changes(&view, &IaitoSeekable::historyChanged);
        view.seek(0x200);
        QCOMPARE(changes.count(), 1);
        view.seekPrev();
        QCOMPARE(changes.count(), 2);
        view.seekNext();
        QCOMPARE(changes.count(), 3);
        view.seekNext();
        view.seek(0x200);
        QCOMPARE(changes.count(), 3);
    }
};

QTEST_MAIN(SeekableHistoryTests)
#include "test_seekablehistory.moc"
