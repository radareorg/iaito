#include "common/Configuration.h"
#include "widgets/VisualNavbar.h"

#include <QGraphicsRectItem>
#include <QGraphicsView>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolTip>

#include <utility>

class NavbarTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

    static bool select(
        VisualNavbar &navbar, const QString &menuTitle, const QString &choice, bool trigger = true)
    {
        bool selected = false;
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &navbar, [&] {
            for (auto *menu : navbar.findChildren<QMenu *>()) {
                if (menu->title() == menuTitle) {
                    for (auto *action : menu->actions()) {
                        if (action->text() == choice) {
                            selected = trigger || action->isChecked();
                            if (trigger) {
                                action->trigger();
                            }
                            break;
                        }
                    }
                }
            }
            for (auto *menu : navbar.findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly)) {
                menu->close();
            }
        });
        timer.start(0);
        QTest::mousePress(&navbar, Qt::RightButton, Qt::NoModifier, QPoint(1, 1));
        QCoreApplication::processEvents();
        return selected;
    }

    static QStringList legendText(VisualNavbar &navbar)
    {
        QStringList text;
        for (auto *label : navbar.findChild<QWidget *>("navbarLegend")->findChildren<QLabel *>()) {
            if (!label->text().isEmpty()) {
                text << label->text();
            }
        }
        return text;
    }

    static QImage labelImage(QLabel *label)
    {
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
        return label->pixmap() ? label->pixmap()->toImage() : QImage();
#elif QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        return label->pixmap(Qt::ReturnByValue).toImage();
#else
        return label->pixmap().toImage();
#endif
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("navbar");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setVisualNavbarLegendVisible(true);
        Core()->cmd("o malloc://4096");
        Core()->setConfig("asm.arch", "x86");
        Core()->setConfig("asm.bits", 64);
        Core()->cmd(
            "ompg +x; wx 90c3 @ 0; af @ 0; CC test @ 0x100; Cs 5 @ 0x200; f test_flag @ 0x300");
    }

    void init()
    {
        QSettings settings;
        for (const char *key :
             {"visualNavbarLane0", "visualNavbarLane1", "visualNavbarRange", "visualNavbarPalette"}) {
            settings.remove(key);
        }
    }

    void disabledLanes_data()
    {
        QTest::addColumn<int>("disabled");
        QTest::addColumn<bool>("vertical");
        for (int lane = 0; lane < 3; ++lane) {
            for (bool vertical : {false, true}) {
                QTest::newRow(qPrintable(QString("%1-%2").arg(lane).arg(vertical)))
                    << lane << vertical;
            }
        }
    }

    void disabledLanes()
    {
        QFETCH(int, disabled);
        QFETCH(bool, vertical);
        VisualNavbar navbar(nullptr);
        navbar.setOrientation(vertical ? Qt::Vertical : Qt::Horizontal);
        navbar.resize(vertical ? QSize(20, 600) : QSize(900, navbar.sizeHint().height()));
        navbar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&navbar));
        QVERIFY(select(navbar, "Range", "io.maps"));
        QVERIFY(select(navbar, "Lane 1", "Analysis metadata"));
        QVERIFY(select(navbar, "Lane 2", "Permissions"));
        QVERIFY(select(navbar, disabled == 1 ? "Lane 2" : "Lane 1", "Disabled"));
        if (disabled == 2) {
            QVERIFY(select(navbar, "Lane 2", "Disabled"));
        }
        auto *view = navbar.findChild<QGraphicsView *>();
        const auto labels
            = navbar.findChild<QWidget *>("navbarLaneLabels")->findChildren<QLabel *>();
        QVERIFY(labels.at(disabled == 1 ? 1 : 0)->isHidden());
        int blocks = 0;
        for (auto *item : view->scene()->items()) {
            auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item);
            if (rect && rect->brush().gradient()) {
                ++blocks;
                QCOMPARE(
                    vertical ? rect->rect().width() : rect->rect().height(),
                    double(vertical ? view->width() : view->height()));
            }
        }
        if (disabled == 2) {
            QCOMPARE(blocks, 0);
            QVERIFY(!navbar.findChild<QWidget *>("navbarLegend")->isVisible());
        } else {
            QVERIFY(blocks > 0);
            const QPoint point
                = view->mapTo(&navbar, QPoint(view->width() * 3 / 4, view->height() * 3 / 4));
            QTest::mouseClick(&navbar, Qt::MiddleButton, Qt::NoModifier, point);
            QVERIFY(
                QToolTip::text().startsWith(disabled == 0 ? "Permissions:" : "Analysis metadata:"));
            if (!vertical) {
                QCOMPARE(labels.at(disabled == 0 ? 1 : 0)->height(), view->height());
            }
        }
    }

    void palettesAndPersistence_data()
    {
        QTest::addColumn<QString>("palette");
        for (const char *palette : {"Default", "Greyscale", "Theme", "Rainbow"}) {
            QTest::newRow(palette) << QString::fromLatin1(palette);
        }
    }

    void palettesAndPersistence()
    {
        QFETCH(QString, palette);
        QImage gradient;
        {
            VisualNavbar navbar(nullptr);
            QVERIFY(select(navbar, "Range", "io.maps"));
            QVERIFY(select(navbar, "Lane 1", "Disabled"));
            QVERIFY(select(navbar, "Lane 2", "Entropy"));
            QVERIFY(select(navbar, "Color palette", palette));
            for (auto *label :
                 navbar.findChild<QWidget *>("navbarLegend")->findChildren<QLabel *>()) {
                const QImage image = labelImage(label);
                if (image.width() > image.height()) {
                    gradient = image;
                }
            }
            QVERIFY(!gradient.isNull());
            QVERIFY(gradient.pixelColor(0, 0) != gradient.pixelColor(gradient.width() - 1, 0));
            if (palette == "Greyscale") {
                QCOMPARE(gradient.pixelColor(0, 0), QColor(1, 1, 1));
                QCOMPARE(gradient.pixelColor(gradient.width() - 1, 0), QColor(255, 255, 255));
                QVERIFY(select(navbar, "Lane 2", "Analysis metadata"));
                Core()->cmd("fc rgb:123456 @ 0x300");
                Core()->triggerRefreshAll();
                for (auto *item : navbar.findChild<QGraphicsView *>()->scene()->items()) {
                    auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item);
                    if (rect && rect->brush().gradient()) {
                        for (const auto &stop : rect->brush().gradient()->stops()) {
                            QCOMPARE(stop.second.red(), stop.second.green());
                            QCOMPARE(stop.second.green(), stop.second.blue());
                        }
                    }
                }
            }
        }
        QSettings settings;
        settings.sync();
        QSettings saved(settings.fileName(), QSettings::IniFormat);
        QCOMPARE(saved.value("visualNavbarLane0").toString(), QStringLiteral("none"));
        QCOMPARE(saved.value("visualNavbarRange").toString(), QStringLiteral("io.maps"));
        VisualNavbar restored(nullptr);
        QVERIFY(select(restored, "Lane 1", "Disabled", false));
        QVERIFY(select(
            restored, "Lane 2", palette == "Greyscale" ? "Analysis metadata" : "Entropy", false));
        QVERIFY(select(restored, "Range", "io.maps", false));
        QVERIFY(select(restored, "Color palette", palette, false));
    }

    void valueSources_data()
    {
        QTest::addColumn<QString>("source");
        for (const char *source :
             {"Entropy (p-)",
              "Entropy",
              "Strings",
              "Printable characters",
              "Unique bytes",
              "0x00 bytes",
              "0xFF bytes",
              "Flags/Marks",
              "Jumps",
              "Analysis density",
              "Basic blocks",
              "Calls",
              "Invalid instructions",
              "Syscalls"}) {
            QTest::newRow(source) << QString::fromLatin1(source);
        }
    }

    void valueSources()
    {
        QFETCH(QString, source);
        VisualNavbar navbar(nullptr);
        QVERIFY(select(navbar, "Lane 1", source));
        QVERIFY(select(navbar, "Lane 2", "Printable characters"));
        const QStringList text = legendText(navbar);
        QVERIFY(!text.join(' ').contains(source));
        QVERIFY(!text.join(' ').contains("Printable characters"));
        const auto laneLabels
            = navbar.findChild<QWidget *>("navbarLaneLabels")->findChildren<QLabel *>();
        QCOMPARE(laneLabels.at(0)->text(), source);
        QCOMPARE(laneLabels.at(1)->text(), QStringLiteral("Printable characters"));
        QCOMPARE(text.count("Zero"), 1);
        QCOMPARE(text.count("Low"), 1);
        QCOMPARE(text.count("High"), 1);
        QVERIFY(!text.contains("Code / functions"));
        int gradients = 0;
        for (auto *label : navbar.findChild<QWidget *>("navbarLegend")->findChildren<QLabel *>()) {
            const QImage image = labelImage(label);
            if (image.width() > image.height()) {
                ++gradients;
                QVERIFY(image.pixelColor(0, 0) != image.pixelColor(image.width() - 1, 0));
            }
        }
        QCOMPARE(gradients, 1);
    }

    void metadataAndPermissions()
    {
        VisualNavbar navbar(nullptr);
        navbar.resize(1000, navbar.sizeHint().height());
        navbar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&navbar));
        QVERIFY(select(navbar, "Range", "io.maps"));
        QVERIFY(select(navbar, "Lane 1", "Analysis metadata"));
        QVERIFY(select(navbar, "Lane 2", "Permissions"));
        QStringList text = legendText(navbar);
        QVERIFY(text.contains("Code / functions"));
        QVERIFY(text.contains("Symbols / flags / comments"));
        QVERIFY(text.contains("Strings"));
        QVERIFY(text.contains("rwx"));
        QVERIFY(!text.contains("Low"));
        QVERIFY(!text.contains("Lane 2: Entropy"));
        if (qEnvironmentVariableIsSet("IAITO_NAVBAR_PREVIEW")) {
            navbar.grab().save(qEnvironmentVariable("IAITO_NAVBAR_PREVIEW") + ".metadata.png");
        }

        Core()->cmd("fc rgb:123456 @ 0x300");
        Core()->triggerRefreshAll();
        QCoreApplication::processEvents();
        auto *legend = navbar.findChild<QWidget *>("navbarLegend");
        bool customColor = false;
        for (auto *label : legend->findChildren<QLabel *>()) {
            const QImage image = labelImage(label);
            customColor |= !image.isNull() && image.pixelColor(0, 0) == QColor("#123456");
        }
        QVERIFY(customColor);

        Config()->setColorTheme("iaito");
        QCoreApplication::processEvents();
        auto *view = navbar.findChild<QGraphicsView *>();
        QSet<QRgb> swatchColors;
        for (auto *label : legend->findChildren<QLabel *>()) {
            const QImage image = labelImage(label);
            if (!image.isNull()) {
                swatchColors.insert(image.pixelColor(0, 0).rgba());
            }
        }
        for (auto *item : view->scene()->items()) {
            auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item);
            if (rect && rect->brush().gradient()) {
                const auto stops = rect->brush().gradient()->stops();
                QVERIFY(swatchColors.contains(stops.at(1).second.rgba()));
            }
        }

        QVERIFY(select(navbar, "Lane 2", "Disabled"));
        const QStringList metadata = legendText(navbar);
        QVERIFY(select(navbar, "Lane 2", "Analysis metadata"));
        QCOMPARE(legendText(navbar), metadata);
    }

    void visibilityAndSpacing()
    {
        VisualNavbar navbar(nullptr);
        navbar.resize(900, navbar.sizeHint().height());
        navbar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&navbar));
        auto *legend = navbar.findChild<QWidget *>("navbarLegend");
        auto *view = navbar.findChild<QGraphicsView *>();
        auto *laneLabels = navbar.findChild<QWidget *>("navbarLaneLabels");
        const int originalThickness = Config()->getVisualNavbarThickness();
        for (const auto &[name, thickness] :
             {std::pair{"Small", 14}, {"Medium", 20}, {"Large", 32}}) {
            QVERIFY(select(navbar, "Thickness", name));
            QCOMPARE(view->height(), thickness);
            QCOMPARE(laneLabels->height(), thickness);
            for (auto *label : laneLabels->findChildren<QLabel *>()) {
                QVERIFY(label->fontMetrics().height() <= thickness / 2);
            }
        }
        Config()->setVisualNavbarThickness(originalThickness);
        QCoreApplication::processEvents();
        QVERIFY(laneLabels->geometry().left() > view->geometry().right());
        QCOMPARE(legend->layout()->contentsMargins().top(), 0);
        QCOMPARE(legend->parentWidget()->layout()->spacing(), 0);
        QCOMPARE(legend->geometry().top(), view->geometry().bottom() + 1);
        const auto labels = legend->findChildren<QLabel *>();
        QVERIFY(!labels.isEmpty());
        for (auto *label : labels) {
            QVERIFY(
                qAbs(label->geometry().center().y() - labels.first()->geometry().center().y()) <= 1);
        }
        QVERIFY(legend->isVisible());
        if (qEnvironmentVariableIsSet("IAITO_NAVBAR_PREVIEW")) {
            navbar.grab().save(qEnvironmentVariable("IAITO_NAVBAR_PREVIEW"));
        }
        navbar.setOrientation(Qt::Vertical);
        QVERIFY(!legend->isVisible());
        navbar.setOrientation(Qt::Horizontal);
        QVERIFY(legend->isVisible());
        Config()->setVisualNavbarLegendVisible(false);
        QVERIFY(!legend->isVisible());
        Config()->setVisualNavbarLegendVisible(true);
        QVERIFY(legend->isVisible());
        QVERIFY(select(navbar, "Lane 1", "Disabled"));
        QCOMPARE(legendText(navbar).count("Low"), 1);
        QVERIFY(select(navbar, "Lane 2", "Disabled"));
        QVERIFY(legendText(navbar).isEmpty());
        QVERIFY(!legend->isVisible());
        QVERIFY(select(navbar, "Lane 1", "Entropy"));
        QVERIFY(legend->isVisible());
    }
};

QTEST_MAIN(NavbarTests)
#include "test_navbar.moc"
