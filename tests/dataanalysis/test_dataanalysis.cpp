#include "common/Configuration.h"
#include "common/DataAnalysis.h"
#include "common/IaitoSeekable.h"
#include "core/Iaito.h"
#include "widgets/DataAnalysisView.h"
#include "widgets/DataAnalysisWidget.h"

#include <QDialog>
#include <QFile>
#include <QGestureEvent>
#include <QLabel>
#include <QMenu>
#include <QNativeGestureEvent>
#include <QPinchGesture>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolTip>
#include <QWheelEvent>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPointingDevice>
#endif

#include <limits>

using namespace DataAnalysis;

class DataAnalysisTest : public QObject
{
    Q_OBJECT
    QTemporaryDir temporary;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("dataanalysis");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
    }

    void byteDistributions()
    {
        QByteArray data;
        for (int i = 0; i < 256; ++i) {
            data.append(char(i));
        }
        auto analyze = [&](const QByteArray &bytes) {
            Range range{0, RVA(bytes.size())};
            return sample(range, {range}, [&bytes](RVA offset, int size) {
                return bytes.mid(int(offset), size);
            });
        };
        auto all = analyze(data);
        QCOMPARE(all.bytes, 256);
        QCOMPARE(all.values[Entropy], 1.0);
        QCOMPARE(all.values[Unique], 1.0);
        QCOMPARE(all.values[Mean], 0.5);
        QCOMPARE(all.values[Printable], 95.0 / 256);
        QCOMPARE(all.values[Alpha], 52.0 / 256);
        QCOMPARE(all.values[Digit], 10.0 / 256);
        QCOMPARE(all.values[Space], 6.0 / 256);
        QCOMPARE(all.values[Control], 33.0 / 256);
        QCOMPARE(all.values[High], 0.5);
        auto zero = analyze(QByteArray(1024, 0));
        QCOMPARE(zero.values[Entropy], 0.0);
        QCOMPARE(zero.values[Zero], 1.0);
        QCOMPARE(zero.values[FF], 0.0);
        auto ff = analyze(QByteArray(1024, char(255)));
        QCOMPARE(ff.values[FF], 1.0);
        QCOMPARE(ff.values[Zero], 0.0);
        QCOMPARE(ff.values[Entropy], 0.0);
        auto binary = analyze(QByteArray(512, 0) + QByteArray(512, 1));
        QCOMPARE(binary.values[Entropy] * 8, 1.0);
    }

    void sparseAndFailedReads()
    {
        const QVector<Range> ranges = mergeRanges({{10, 20}, {15, 25}, {80, 90}, {30, 30}});
        QCOMPARE(ranges.size(), 2);
        int calls = 0;
        bool validReads = true;
        auto result = sample({0, 100}, ranges, [&](RVA offset, int size) {
            ++calls;
            validReads &= (offset >= 10 && offset + size <= 25)
                          || (offset >= 80 && offset + size <= 90);
            return QByteArray(size, 0);
        });
        QVERIFY(validReads);
        QCOMPARE(result.covered, RVA(25));
        QCOMPARE(result.bytes, 25);
        QCOMPARE(calls, 2);
        QCOMPARE(result.values[Zero], 1.0);
        result = sample({30, 70}, ranges, [&](RVA, int size) {
            ++calls;
            return QByteArray(size, char(255));
        });
        QCOMPARE(result.bytes, 0);
        QCOMPARE(calls, 2);
        result = sample({0, 100}, ranges, [](RVA, int) { return QByteArray(); });
        QCOMPARE(result.bytes, 0);
        QCOMPARE(result.values[FF], 0.0);
        result = sample({0, 100}, ranges, [](RVA, int) { return QByteArray(2, 'a'); });
        QCOMPARE(result.bytes, 4);
        QCOMPARE(result.values[Printable], 1.0);
    }

    void boundedSamplingAndHugeAddresses()
    {
        const Range enormous{0, RVA_MAX};
        int requested = 0;
        auto result = sample(enormous, {enormous}, [&](RVA address, int size) {
            requested += size;
            Q_ASSERT(address <= RVA_MAX - size);
            return QByteArray(size, 'x');
        });
        QCOMPARE(requested, 4096);
        QCOMPARE(result.covered, RVA_MAX);
        QCOMPARE(result.bytes, 4096);
        RVA previous = 0;
        for (int i = 0; i < 4096; ++i) {
            const auto part = bucket(enormous, i, 4096);
            QCOMPARE(part.start, previous);
            QVERIFY(part.valid());
            previous = part.end;
        }
        QCOMPARE(previous, RVA_MAX);
        const Range high{RVA_MAX - 100, RVA_MAX};
        QVERIFY(centered(high, RVA_MAX - 1, 10) == (Range{RVA_MAX - 10, RVA_MAX}));
        QVERIFY(centered(high, 0, 10) == (Range{RVA_MAX - 100, RVA_MAX - 90}));
        QVERIFY(sizedRange(RVA_MAX - 5, 100) == (Range{RVA_MAX - 5, RVA_MAX}));
        QVERIFY(!intersect({0, 10}, {20, 30}).valid());
        const auto small = sample({0, 4}, {{0, 4}}, [](RVA, int n) { return QByteArray(n, 'a'); });
        QCOMPARE(small.bytes, 4);
    }

    void plotNavigation()
    {
        DataAnalysisView view(false);
        view.resize(800, 400);
        view.domain = {RVA_MAX - 10000, RVA_MAX};
        view.visibleRange = {RVA_MAX - 5000, RVA_MAX - 4000};
        QCOMPARE(view.addressAt(view.plotRect().left()), view.visibleRange.start);
        QCOMPARE(view.addressAt(view.plotRect().right()), view.visibleRange.end - 1);
        QCOMPARE(view.addressAt(view.xForAddress(RVA_MAX - 4500)), RVA_MAX - 4500);
        QSignalSpy rangeSpy(&view, &DataAnalysisView::rangeRequested);
        view.zoom(0.5, RVA_MAX - 4500);
        auto range = rangeSpy.takeFirst();
        QCOMPARE(range[0].toULongLong(), RVA_MAX - 4750);
        QCOMPARE(range[1].toULongLong(), RVA_MAX - 4250);
        view.zoom(100, RVA_MAX - 4500);
        range = rangeSpy.takeFirst();
        QCOMPARE(range[0].toULongLong(), view.domain.start);
        QCOMPARE(range[1].toULongLong(), view.domain.end);
        view.visibleRange = {RVA_MAX - 1, RVA_MAX};
        view.zoom(0.5, RVA_MAX - 1);
        range = rangeSpy.takeFirst();
        QCOMPARE(range[0].toULongLong(), RVA_MAX - 1);
        QCOMPARE(range[1].toULongLong(), RVA_MAX);

        view.domain = view.visibleRange = {0, 1000};
        view.show();
        QSignalSpy selectionSpy(&view, &DataAnalysisView::selectionRequested);
        const QPoint from(view.xForAddress(200), 100), to(view.xForAddress(700), 100);
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to);
        QCOMPARE(selectionSpy.size(), 1);
        const auto selection = selectionSpy.takeFirst();
        QVERIFY(selection[0].toULongLong() <= 200);
        QVERIFY(selection[1].toULongLong() >= 699);
        QSignalSpy seekSpy(&view, &DataAnalysisView::seekRequested);
        view.markers = {{{250, 251}, "hit.search", DataAnalysisView::Flag}};
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(view.xForAddress(250), 100));
        QCOMPARE(seekSpy.takeFirst()[0].toULongLong(), RVA(250));

        DataAnalysisView mini(true);
        mini.resize(800, 80);
        mini.domain = {0, 1000};
        mini.visibleRange = {200, 400};
        mini.show();
        QSignalSpy miniSpy(&mini, &DataAnalysisView::rangeRequested);
        QTest::mousePress(&mini, Qt::LeftButton, Qt::NoModifier, QPoint(mini.xForAddress(300), 20));
        QTest::mouseRelease(&mini, Qt::LeftButton, Qt::NoModifier, QPoint(mini.xForAddress(400), 20));
        const auto moved = miniSpy.takeLast();
        QVERIFY(moved[0].toULongLong() >= 299 && moved[0].toULongLong() <= 301);
        QCOMPARE(moved[1].toULongLong() - moved[0].toULongLong(), RVA(200));
    }

    void wheelNavigation_data()
    {
        QTest::addColumn<bool>("overview");
        QTest::newRow("plot") << false;
        QTest::newRow("overview") << true;
    }

    void wheelNavigation()
    {
        QFETCH(bool, overview);
        DataAnalysisView view(overview);
        view.resize(800, 400);
        view.domain = {RVA_MAX - 1000, RVA_MAX};
        view.visibleRange = {RVA_MAX - 600, RVA_MAX - 200};
        QSignalSpy rangeSpy(&view, &DataAnalysisView::rangeRequested);
        connect(&view, &DataAnalysisView::rangeRequested, &view, [&](RVA start, RVA end) {
            view.visibleRange = {start, end};
        });
        auto wheel = [&](QPoint delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            const QPointF position(400, 50);
            QWheelEvent event(
                position,
                view.mapToGlobal(position.toPoint()),
                {},
                delta,
                Qt::NoButton,
                modifiers,
                Qt::NoScrollPhase,
                false);
            QApplication::sendEvent(&view, &event);
            return event.isAccepted();
        };
        QVERIFY(wheel({0, 120}));
        QCOMPARE(view.visibleRange.start, RVA_MAX - 700);
        QCOMPARE(view.visibleRange.size(), RVA(400));
        QVERIFY(wheel({0, -120}));
        QCOMPARE(view.visibleRange.start, RVA_MAX - 600);
        QCOMPARE(view.visibleRange.size(), RVA(400));
        QVERIFY(wheel({120, 0}, Qt::ShiftModifier));
        QCOMPARE(view.visibleRange.start, RVA_MAX - 700);
        QCOMPARE(view.visibleRange.size(), RVA(400));
        QVERIFY(wheel({0, -120}, Qt::ControlModifier));
        QCOMPARE(view.visibleRange.start, RVA_MAX - 600);
        QCOMPARE(view.visibleRange.size(), RVA(400));

        view.visibleRange = {RVA_MAX - 450, RVA_MAX - 50};
        QVERIFY(wheel({0, -120}));
        QCOMPARE(view.visibleRange.start, RVA_MAX - 400);
        QCOMPARE(view.visibleRange.end, RVA_MAX);
        view.visibleRange = {view.domain.start + 50, view.domain.start + 450};
        QVERIFY(wheel({0, 120}));
        QCOMPARE(view.visibleRange.start, view.domain.start);
        QCOMPARE(view.visibleRange.size(), RVA(400));
        rangeSpy.clear();
        QVERIFY(!wheel({}));
        QVERIFY(rangeSpy.isEmpty());
    }

    void pinchNavigation_data()
    {
        QTest::addColumn<bool>("overview");
        QTest::addColumn<bool>("native");
        QTest::newRow("plot-touch") << false << false;
        QTest::newRow("overview-touch") << true << false;
        QTest::newRow("plot-trackpad") << false << true;
        QTest::newRow("overview-trackpad") << true << true;
    }

    void pinchNavigation()
    {
        QFETCH(bool, overview);
        QFETCH(bool, native);
        DataAnalysisView view(overview);
        if (!native) {
            // Exercise the touch path even on macOS, which normally uses native events.
            view.grabGesture(Qt::PinchGesture);
        }
        view.resize(860, 400);
        view.move(180, 100);
        view.domain = {RVA_MAX - 10000, RVA_MAX};
        view.visibleRange = {RVA_MAX - 6000, RVA_MAX - 2000};
        view.show();
        QSignalSpy rangeSpy(&view, &DataAnalysisView::rangeRequested);
        connect(&view, &DataAnalysisView::rangeRequested, &view, [&](RVA start, RVA end) {
            view.visibleRange = {start, end};
        });
        auto pinch = [&](qreal scale) {
            const QPointF position(view.xForAddress(RVA_MAX - 5000), 50);
            const QPointF global = view.mapToGlobal(position.toPoint());
            if (native) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
                QNativeGestureEvent event(
                    Qt::ZoomNativeGesture,
                    QPointingDevice::primaryPointingDevice(),
                    2,
                    position,
                    position,
                    global,
                    scale - 1,
                    {});
#elif QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                QNativeGestureEvent event(
                    Qt::ZoomNativeGesture,
                    QPointingDevice::primaryPointingDevice(),
                    position,
                    position,
                    global,
                    scale - 1,
                    0,
                    0);
#else
                QNativeGestureEvent event(
                    Qt::ZoomNativeGesture, nullptr, position, position, global, scale - 1, 0, 0);
#endif
                QApplication::sendEvent(&view, &event);
                QVERIFY(event.isAccepted());
            } else {
                QPinchGesture gesture;
                gesture.setCenterPoint(global);
                gesture.setScaleFactor(scale);
                gesture.setChangeFlags(QPinchGesture::ScaleFactorChanged);
                QGestureEvent event({&gesture});
                QApplication::sendEvent(&view, &event);
                QVERIFY(event.isAccepted(&gesture));
            }
        };
        pinch(2);
        QCOMPARE(rangeSpy.size(), 1);
        QCOMPARE(view.visibleRange.start, RVA_MAX - 5500);
        QCOMPARE(view.visibleRange.end, RVA_MAX - 3500);
        pinch(2);
        QCOMPARE(rangeSpy.size(), 2);
        QVERIFY(view.visibleRange == (Range{RVA_MAX - 5250, RVA_MAX - 4250}));
        pinch(0.5);
        QCOMPARE(rangeSpy.size(), 3);
        QVERIFY(view.visibleRange == (Range{RVA_MAX - 5500, RVA_MAX - 3500}));
        pinch(0.01);
        QVERIFY(view.visibleRange == view.domain);
        pinch(1e6);
        QCOMPARE(view.visibleRange.size(), RVA(1));
        pinch(0.99);
        QCOMPARE(view.visibleRange.size(), RVA(2));

        rangeSpy.clear();
        for (qreal scale :
             {qreal(0),
              qreal(-1),
              qreal(1),
              std::numeric_limits<qreal>::infinity(),
              std::numeric_limits<qreal>::quiet_NaN()}) {
            pinch(scale);
        }
        QVERIFY(rangeSpy.isEmpty());
        view.domain = view.visibleRange = {};
        pinch(2);
        QVERIFY(rangeSpy.isEmpty());
    }

    void widgetIntegration()
    {
        QFile file(temporary.filePath("fixture.bin"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray bytes(65536, 0);
        for (int i = 16384; i < 32768; ++i) {
            bytes[i] = char(i % 256);
        }
        for (int i = 32768; i < 49152; ++i) {
            bytes[i] = char('a' + i % 26);
        }
        std::fill(bytes.begin() + 49152, bytes.end(), char(255));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        QVERIFY(Core()->loadFile(file.fileName(), 0, 0, R_PERM_R, 0, false, false));
        Core()->cmd("fs analysis-test");
        Core()->cmd("f hit.test 8 @ 0x100");
        Core()->cmd("CCu test comment @ 0x200");
        const QString originalFlagSpace = Core()->cmd("fs.");
        Core()->seek(32);
        const RVA originalSeek = Core()->getOffset();
        const int originalBlock = Core()->core()->blocksize;
        Config()->setAddressRangeSelectionSyncEnabled(true);
        DataAnalysisWidget widget(nullptr);
        widget.resize(960, 640);
        auto *plot = widget.findChild<DataAnalysisView *>("analysisPlot");
        auto *mini = widget.findChild<DataAnalysisView *>("analysisOverview");
        QVERIFY(plot && mini);
        QTest::qWait(150);
        QVERIFY(!plot->domain.valid());
        QVERIFY(plot->samples.isEmpty());
        widget.show();
        QTRY_VERIFY_WITH_TIMEOUT(!plot->samples.isEmpty() && !plot->loading, 10000);
        QCOMPARE(plot->domain.start, RVA(0));
        QCOMPARE(plot->domain.end, RVA(bytes.size()));
        QCOMPARE(Core()->getOffset(), originalSeek);
        QCOMPARE(Core()->core()->blocksize, originalBlock);
        QCOMPARE(Core()->cmd("fs."), originalFlagSpace);
        QCOMPARE(plot->samples.first().values[Zero], 1.0);
        QCOMPARE(plot->samples.last().values[FF], 1.0);
        bool flagFound = false, commentFound = false;
        for (const auto &marker : plot->markers) {
            flagFound |= marker.type == DataAnalysisView::Flag && marker.name == "hit.test";
            commentFound |= marker.type == DataAnalysisView::Comment
                            && marker.name == "test comment";
        }
        QVERIFY(flagFound);
        QVERIFY(commentFound);
        auto *legend = widget.findChild<QToolBar *>("legend");
        QVERIFY(legend);
        QCOMPARE(legend->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
        QAction *entry = legend->actions().first();
        QVERIFY(!entry->isCheckable());
        QVERIFY(entry->toolTip().contains("entropy", Qt::CaseInsensitive));
        bool settingsOpened = false;
        QTimer::singleShot(0, &widget, [&]() {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            settingsOpened = dialog && dialog->objectName() == "analysisMetricSettings";
            if (dialog) {
                dialog->reject();
            }
        });
        const auto before = plot->metrics;
        entry->trigger();
        QVERIFY(settingsOpened);
        QCOMPARE(plot->metrics, before);
        Core()->setAddressRangeSelection(100, 199);
        QVERIFY(plot->selection == (Range{100, 200}));
        emit plot->selectionRequested(200, 300);
        QCOMPARE(Core()->getAddressRangeSelectionStart(), RVA(200));
        QCOMPARE(Core()->getAddressRangeSelectionEnd(), RVA(299));
        widget.getSeekable()->setSynchronization(false);
        emit plot->selectionRequested(400, 500);
        QCOMPARE(Core()->getAddressRangeSelectionStart(), RVA(200));
        widget.getSeekable()->setSynchronization(true);

        widget.showRange(16384, 32767);
        QTRY_VERIFY_WITH_TIMEOUT(!plot->loading && !plot->samples.isEmpty(), 10000);
        QVERIFY(plot->visibleRange == (Range{16384, 32768}));
        QVERIFY(mini->domain == (Range{0, 65536}));
        mini->grabGesture(Qt::PinchGesture);
        QPinchGesture pinch;
        pinch.setCenterPoint(mini->mapToGlobal(mini->plotRect().center().toPoint()));
        pinch.setScaleFactor(2);
        pinch.setChangeFlags(QPinchGesture::ScaleFactorChanged);
        QGestureEvent pinchEvent({&pinch});
        QApplication::sendEvent(mini, &pinchEvent);
        QCOMPARE(plot->visibleRange.size(), RVA(8192));
        QVERIFY(plot->visibleRange == mini->visibleRange);
        QTRY_VERIFY_WITH_TIMEOUT(!plot->loading && !plot->samples.isEmpty(), 10000);
        QCOMPARE(plot->samples.first().range.start, plot->visibleRange.start);
        QCOMPARE(plot->samples.last().range.end, plot->visibleRange.end);
        emit mini->rangeRequested(0, 8192);
        widget.hide();
        QTest::qWait(150);
        QVERIFY(plot->samples.isEmpty());
        widget.show();
        emit mini->rangeRequested(49152, 65536);
        QTRY_VERIFY_WITH_TIMEOUT(!plot->loading && !plot->samples.isEmpty(), 10000);
        QCOMPARE(plot->samples.first().range.start, RVA(49152));
        QCOMPARE(plot->samples.first().values[FF], 1.0);

        for (int resolution : {32, 64}) {
            auto lowResolution = widget.serializeViewProprties();
            lowResolution["resolution"] = resolution;
            widget.deserializeViewProperties(lowResolution);
            QTRY_VERIFY_WITH_TIMEOUT(!plot->loading && plot->samples.size() == resolution, 10000);
            QCOMPARE(widget.serializeViewProprties()["resolution"].toInt(), resolution);
        }

        auto properties = widget.serializeViewProprties();
        properties["metrics"] = QVariantList{Entropy, Printable, Zero, FF};
        properties["style"] = int(DataAnalysisView::Bars);
        properties["tracks"] = true;
        properties["colorMode"] = 1;
        properties["resolution"] = 256;
        widget.deserializeViewProperties(properties);
        QCOMPARE(widget.serializeViewProprties(), properties);
        QTRY_VERIFY_WITH_TIMEOUT(!plot->loading, 10000);
        QVERIFY(widget.grab().save(temporary.filePath("analysis-bars.png")));
        widget.deserializeViewProperties({});
        QVERIFY(!plot->separateTracks);
        QCOMPARE(plot->style, DataAnalysisView::Areas);
        QCOMPARE(plot->metrics.size(), 3);
        widget.showRange(0, 65535);
        QTRY_VERIFY_WITH_TIMEOUT(!plot->loading && !plot->samples.isEmpty(), 10000);
        // Optional artifacts for local visual review; never touch application settings.
        if (qEnvironmentVariableIsSet("IAITO_ANALYSIS_SCREENSHOT")) {
            widget.grab().save(qEnvironmentVariable("IAITO_ANALYSIS_SCREENSHOT"));
            QPalette dark = widget.palette();
            dark.setColor(QPalette::Window, QColor("#191d25"));
            dark.setColor(QPalette::Base, QColor("#10141c"));
            dark.setColor(QPalette::Text, QColor("#d6dde8"));
            dark.setColor(QPalette::WindowText, QColor("#d6dde8"));
            dark.setColor(QPalette::ButtonText, QColor("#d6dde8"));
            dark.setColor(QPalette::Button, QColor("#242c3c"));
            widget.setPalette(dark);
            widget.grab().save(qEnvironmentVariable("IAITO_ANALYSIS_SCREENSHOT") + ".dark.png");
        }
        Core()->clearAddressRangeSelection();
    }

    void mappedDataAndHoles()
    {
        Core()->setConfig("io.va", true);
        const int fd = r_io_fd_get_current(Core()->core()->io);
        Core()->cmd("om-*");
        Core()->cmd(QString("om %1 0x1000 0x100 0 r zeros").arg(fd));
        Core()->cmd(QString("om %1 0x2000 0x100 0xc000 r ones").arg(fd));
        DataAnalysisWidget widget(nullptr);
        widget.resize(800, 600);
        widget.show();
        auto *plot = widget.findChild<DataAnalysisView *>("analysisPlot");
        QTRY_VERIFY_WITH_TIMEOUT(!plot->loading && !plot->samples.isEmpty(), 10000);
        QVERIFY(plot->domain == (Range{0x1000, 0x2100}));
        QCOMPARE(plot->samples.first().values[Zero], 1.0);
        QCOMPARE(plot->samples.last().values[FF], 1.0);
        RVA covered = 0, read = 0;
        for (const auto &sample : plot->samples) {
            covered += sample.covered;
            read += sample.bytes;
        }
        QCOMPARE(covered, RVA(512));
        QCOMPARE(read, RVA(512));
        widget.showRange(0x1800, 0x18ff);
        QTRY_VERIFY_WITH_TIMEOUT(!plot->loading && !plot->samples.isEmpty(), 10000);
        for (const auto &sample : plot->samples) {
            QCOMPARE(sample.bytes, 0);
        }
        auto *status = widget.findChild<QLabel *>("statusLabel");
        QVERIFY(status->text().contains("No readable bytes"));
    }

    void tooltipDelay()
    {
        DataAnalysisView view(false);
        view.domain = view.visibleRange = {0, 1000};
        view.resize(800, 400);
        view.show();
        QTest::mouseMove(&view, QPoint(200, 100));
        QTest::qWait(200);
        QVERIFY(!QToolTip::isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(QToolTip::isVisible(), 1500);
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(200, 100));
        QTRY_VERIFY(!QToolTip::isVisible());
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(200, 100));
    }
};

QTEST_MAIN(DataAnalysisTest)
#include "test_dataanalysis.moc"
