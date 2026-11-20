#include "common/Configuration.h"
#include "common/IaitoSeekable.h"
#include "widgets/DisassemblerGraphView.h"
#include "widgets/SimpleTextGraphView.h"

#include <QAction>
#include <QImage>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTest>

using Style = Configuration::GraphEdgeStyle;

class EdgeGraph : public IaitoGraphView
{
public:
    EdgeGraph()
        : IaitoGraphView(nullptr)
    {
        backgroundColor = Qt::white;
        width = height = 160;
        GraphBlock from;
        from.entry = 0;
        from.edges.emplace_back(1);
        from.edges.back().polyline = QPolygonF({{30, 20}, {30, 60}, {110, 60}, {110, 100}});
        blocks.emplace(0, from);
        GraphBlock to;
        to.entry = 1;
        blocks.emplace(1, to);
    }

    void transform(const QTransform &transform, GraphEdge::ArrowDirection arrow)
    {
        auto &edge = blocks.at(0).edges.front();
        edge.polyline = transform.map(edge.polyline);
        edge.arrow = arrow;
    }

    void repeatPoints()
    {
        auto &points = blocks.at(0).edges.front().polyline;
        points.insert(1, points.first());
        points.insert(3, points[2]);
        points.append(points.last());
    }

    QImage renderEdges()
    {
        QImage image(width, height, QImage::Format_ARGB32);
        image.fill(backgroundColor);
        QPainter painter(&image);
        paint(painter, {}, image.rect(), 1, false);
        return image;
    }

    void drawBlock(QPainter &, GraphBlock &, bool) override {}
    EdgeConfiguration edgeConfiguration(GraphBlock &, GraphBlock *, bool) override
    {
        EdgeConfiguration config;
        config.color = Qt::black;
        config.width_scale = 2;
        return config;
    }
};

class TextGraph : public SimpleTextGraphView
{
public:
    TextGraph()
        : SimpleTextGraphView(nullptr, nullptr)
    {}
    void loadCurrentGraph() override {}
};

class GraphLineTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

    static QMenu *styleMenu(QObject &graph)
    {
        return graph.findChild<QMenu *>(QStringLiteral("graphEdgeStyleMenu"));
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("graphlines");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
    }

    void init() { Config()->setGraphEdgeStyle(Style::Square); }

    void menusAndPersistence()
    {
        IaitoSeekable seekable;
        DisassemblerGraphView graph(nullptr, &seekable, nullptr, {});
        TextGraph other;
        auto *menu = styleMenu(graph);
        auto *otherMenu = styleMenu(other);
        QVERIFY(menu && otherMenu);
        QCOMPARE(menu->actions().size(), 3);
        QCOMPARE(menu->actions()[0]->text(), QStringLiteral("Square"));
        QCOMPARE(menu->actions()[1]->text(), QStringLiteral("Round"));
        QCOMPARE(menu->actions()[2]->text(), QStringLiteral("Circuit"));
        for (QObject *view : {static_cast<QObject *>(&graph), static_cast<QObject *>(&other)}) {
            auto *submenu = styleMenu(*view);
            int count = 0;
            for (auto *parentMenu : view->findChildren<QMenu *>()) {
                if (parentMenu->actions().contains(submenu->menuAction())) {
                    ++count;
                }
            }
            QCOMPARE(count, 2); // Background and block menus.
        }
        const auto jumpStyle = Config()->getDisassemblyJumpLineStyle();
        for (int selected : {1, 2, 0}) {
            menu->actions()[selected]->trigger();
            QCOMPARE(int(Config()->getGraphEdgeStyle()), selected);
            QCOMPARE(QSettings().value("graphEdgeStyle").toInt(), selected);
            for (int i = 0; i < 3; ++i) {
                QCOMPARE(menu->actions()[i]->isChecked(), i == selected);
                QCOMPARE(otherMenu->actions()[i]->isChecked(), i == selected);
            }
            EdgeGraph reopened;
            QVERIFY(styleMenu(reopened)->actions()[selected]->isChecked());
            QCOMPARE(Config()->getDisassemblyJumpLineStyle(), jumpStyle);
        }
        QSettings().setValue("graphEdgeStyle", 99);
        QCOMPARE(Config()->getGraphEdgeStyle(), Style::Square);
    }

    void rendering_data()
    {
        QTest::addColumn<QTransform>("transform");
        QTest::addColumn<int>("arrow");
        QTest::newRow("down") << QTransform() << int(GraphView::GraphEdge::Down);
        QTest::newRow("right") << QTransform(0, -1, 1, 0, 0, 160)
                               << int(GraphView::GraphEdge::Right);
        QTest::newRow("up") << QTransform(-1, 0, 0, -1, 160, 160) << int(GraphView::GraphEdge::Up);
        QTest::newRow("left") << QTransform(0, 1, -1, 0, 160, 0) << int(GraphView::GraphEdge::Left);
    }

    void rendering()
    {
        QFETCH(QTransform, transform);
        QFETCH(int, arrow);
        EdgeGraph graph;
        graph.transform(transform, GraphView::GraphEdge::ArrowDirection(arrow));
        QList<QImage> images;
        for (Style style : {Style::Square, Style::Round, Style::Circuit}) {
            Config()->setGraphEdgeStyle(style);
            const QImage image = graph.renderEdges();
            const QPoint stem = transform.map(QPoint(30, 35));
            const QPoint tip = transform.map(QPoint(110, 98));
            const QPoint empty = transform.map(QPoint(70, 40));
            QCOMPARE(image.pixelColor(stem), QColor(Qt::black));
            QVERIFY(image.pixelColor(tip).lightness() < 30);
            QCOMPARE(image.pixelColor(empty), QColor(Qt::white)); // No filled edge interior.
            const QPoint corner = transform.map(QPoint(30, 60));
            const QPoint diagonal = transform.map(QPoint(35, 55));
            if (style == Style::Square) {
                QVERIFY(image.pixelColor(corner).lightness() < 30);
            } else {
                QCOMPARE(image.pixelColor(corner), QColor(Qt::white));
            }
            if (style == Style::Circuit) {
                QVERIFY(image.pixelColor(diagonal).lightness() < 128);
            } else {
                QCOMPARE(image.pixelColor(diagonal), QColor(Qt::white));
            }
            for (const auto &previous : images) {
                QVERIFY(image != previous);
            }
            images.append(image);

            const QString base = settings.filePath(
                QStringLiteral("%1-%2").arg(QTest::currentDataTag()).arg(int(style)));
            graph.saveAsBitmap(base + ".png", "PNG");
            QCOMPARE(QImage(base + ".png").convertToFormat(QImage::Format_ARGB32), image);
            graph.saveAsSvg(base + ".svg");
            QSvgRenderer svg(base + ".svg");
            QVERIFY(svg.isValid());
            QImage exported(160, 160, QImage::Format_ARGB32);
            exported.fill(Qt::white);
            QPainter painter(&exported);
            svg.render(&painter);
            painter.end();
            QVERIFY(exported.pixelColor(stem).lightness() < 30);
            QVERIFY(exported.pixelColor(tip).lightness() < 30);
            QCOMPARE(exported.pixelColor(empty), QColor(Qt::white));
        }
        graph.repeatPoints();
        QCOMPARE(graph.renderEdges(), images.last());
    }

    void cachedViewsRepaint()
    {
        EdgeGraph first;
        EdgeGraph second;
        first.resize(180, 180);
        second.resize(180, 180);
        first.show();
        second.show();
        const QImage square = first.viewport()->grab().toImage();
        const QImage secondSquare = second.viewport()->grab().toImage();
        QSignalSpy refreshed(&first, &IaitoGraphView::viewRefreshed);
        Config()->setGraphEdgeStyle(Style::Round);
        QCoreApplication::processEvents();
        QVERIFY(first.viewport()->grab().toImage() != square);
        QVERIFY(second.viewport()->grab().toImage() != secondSquare);
        QCOMPARE(refreshed.count(), 0); // Repainting does not reload or rearrange the graph.
    }
};

QTEST_MAIN(GraphLineTests)
#include "test_graphlines.moc"
