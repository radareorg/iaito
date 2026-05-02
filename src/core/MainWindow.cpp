#include "core/MainWindow.h"
#include "ui_MainWindow.h"

// Common Headers
#include "IaitoApplication.h"
#include "common/BugReporting.h"
#include "common/Helpers.h"
#include "common/ProgressIndicator.h"
#include "common/RunScriptTask.h"
#include "common/TempConfig.h"
#include "plugins/IaitoPlugin.h"
#include "plugins/PluginManager.h"

// Dialogs
#include "dialogs/AboutDialog.h"
#include "dialogs/AddressSpaceManagerDialog.h"
#include "dialogs/AssemblerDialog.h"
#include "dialogs/AsyncTaskDialog.h"
#include "dialogs/CommentsDialog.h"
#include "dialogs/DumpDialog.h"
#include "dialogs/EditInstructionDialog.h"
#include "dialogs/FlagDialog.h"
#include "dialogs/FortuneDialog.h"
#include "dialogs/InitialOptionsDialog.h"
#include "dialogs/LayoutManager.h"
#include "dialogs/MapFileDialog.h"
#include "dialogs/NewFileDialog.h"
#include "dialogs/PackageManagerDialog.h"
#include "dialogs/SaveProjectDialog.h"
#include "dialogs/ScriptManagerDialog.h"
#include "dialogs/WelcomeDialog.h"
#include "dialogs/XrefsDialog.h"
#include "dialogs/settings/SettingsDialog.h"

// Widgets Headers
#include "widgets/BacktraceWidget.h"
#include "widgets/BinariesWidget.h"
#include "widgets/BreakpointWidget.h"
#include "widgets/CalculatorWidget.h"
#include "widgets/CallGraph.h"
#include "widgets/ClassesWidget.h"
#include "widgets/CommentsWidget.h"
#include "widgets/ConsoleWidget.h"
#include "widgets/CustomCommandWidget.h"
#include "widgets/Dashboard.h"
#include "widgets/DebugActions.h"
#include "widgets/DecompilerWidget.h"
#include "widgets/DisassemblerGraphView.h"
#include "widgets/DisassemblyWidget.h"
#include "widgets/EntrypointWidget.h"
#include "widgets/ExportsWidget.h"
#include "widgets/FilesWidget.h"
#include "widgets/FilesystemWidget.h"
#include "widgets/FlagsWidget.h"
#include "widgets/FunctionsWidget.h"
#include "widgets/GraphView.h"
#include "widgets/GraphWidget.h"
#include "widgets/HeadersWidget.h"
#include "widgets/HexdumpWidget.h"
#include "widgets/ImportsWidget.h"
#include "widgets/MapsWidget.h"
#include "widgets/MemoryMapWidget.h"
#include "widgets/Omnibar.h"
#include "widgets/OverviewWidget.h"
#include "widgets/ProcessesWidget.h"
#include "widgets/R2AIWidget.h"
#include "widgets/R2GraphWidget.h"
#include "widgets/RefsWidget.h"
#include "widgets/RegisterRefsWidget.h"
#include "widgets/RegistersWidget.h"
#include "widgets/RelocsWidget.h"
#include "widgets/ResourcesWidget.h"
#include "widgets/SdbWidget.h"
#include "widgets/SearchWidget.h"
#include "widgets/SectionsWidget.h"
#include "widgets/SegmentsWidget.h"
#include "widgets/StackWidget.h"
#include "widgets/StringsWidget.h"
#include "widgets/SymbolsWidget.h"
#include "widgets/SyscallsWidget.h"
#include "widgets/ThreadsWidget.h"
#include "widgets/TypesWidget.h"
#include "widgets/VTablesWidget.h"
#include "widgets/VisualNavbar.h"
#include "widgets/XrefsWidget.h"
#include "widgets/ZignaturesWidget.h"
#include "widgets/ZoomWidget.h"

// Qt Headers
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPolygonF>
#include <QProcess>
#include <QPropertyAnimation>
#include <QSysInfo>
#include <QTcpServer>

#include <QKeyEvent>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QStringListModel>
#include <QStyleFactory>
#include <QStyledItemDelegate>
#include <QSvgRenderer>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidgetItem>
#include <QVector>
#include <QtGlobal>

// Graphics
#include <cmath>

#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsView>

template<class T>
T *getNewInstance(MainWindow *m)
{
    return new T(m);
}

namespace {

struct AnalyzePluginCommandEntry
{
    QString name;
    QString args;
    QString description;
};

struct AnalyzePluginPlaceholder
{
    QString label;
    bool optional = false;
};

static const char *analyzePluginDynamicActionProperty = "analyzePluginDynamicAction";
static const char *recentScriptDynamicActionProperty = "recentScriptDynamicAction";
static const char *recentScriptsSettingsKey = "recentScriptList";
static constexpr int defaultSideDockWidth = 200;
static constexpr int maxRecentScripts = 8;

enum class AppMenuIcon {
    Open,
    Map,
    Manage,
    Save,
    Import,
    Export,
    Run,
    Script,
    Commit,
    Settings,
    Quit,
    NavigateBack,
    NavigateForward,
    Undo,
    Redo,
    Patch,
    IOMode,
    Highlight,
    View,
    Info,
    Storage,
    Extra,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    Analysis,
    Function,
    Autoname,
    Plugin,
    Package,
    Calculator,
    Syscalls,
    Assembler,
    Web,
    Layout,
    Refresh,
    Lock,
    Reset,
    Issue,
    Website,
    Book,
    Fortune,
    Update,
    About,
    Debug,
    Profile,
    Search,
    Xrefs,
    Refs,
    Console,
    Dashboard,
    Graph,
    Disassembly,
    Hexdump,
    Decompiler,
    CustomCommand,
};

QIcon makeAppMenuIcon(const QWidget *widget, AppMenuIcon icon, const QColor &color)
{
    const qreal ratio = widget ? qhelpers::devicePixelRatio(widget) : qreal(1.0);
    const int size = 16;
    QPixmap pixmap(size * ratio, size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor accent = color;
    accent.setAlpha(215);
    QPen pen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    auto drawDocument = [&painter]() {
        painter.drawPolyline(QPolygonF(
            QVector<QPointF>{
                QPointF(4, 2.5),
                QPointF(10, 2.5),
                QPointF(12, 4.5),
                QPointF(12, 13.5),
                QPointF(4, 13.5),
                QPointF(4, 2.5)}));
        painter.drawLine(QPointF(10, 2.5), QPointF(10, 5));
        painter.drawLine(QPointF(10, 5), QPointF(12, 5));
    };
    auto drawTray = [&painter]() {
        painter.drawLine(QPointF(4, 11.5), QPointF(12, 11.5));
        painter.drawLine(QPointF(4, 11.5), QPointF(4, 9.5));
        painter.drawLine(QPointF(12, 11.5), QPointF(12, 9.5));
    };
    auto drawPlay = [&painter, &accent]() {
        painter.setBrush(accent);
        painter.drawPolygon(
            QPolygonF(QVector<QPointF>{QPointF(5, 3.5), QPointF(12, 8), QPointF(5, 12.5)}));
        painter.setBrush(Qt::NoBrush);
    };
    auto drawMagnifier = [&painter]() {
        painter.drawEllipse(QRectF(3.5, 3.5, 7, 7));
        painter.drawLine(QPointF(9.2, 9.2), QPointF(13, 13));
    };
    constexpr qreal pi = 3.14159265358979323846;

    switch (icon) {
    case AppMenuIcon::Open:
        painter.drawPolyline(QPolygonF(
            QVector<QPointF>{
                QPointF(2.5, 6),
                QPointF(5.5, 6),
                QPointF(6.8, 4.2),
                QPointF(10.5, 4.2),
                QPointF(12.5, 6),
                QPointF(13.5, 6)}));
        painter.drawPolyline(QPolygonF(
            QVector<QPointF>{
                QPointF(3, 6.5),
                QPointF(13, 6.5),
                QPointF(11.5, 12.5),
                QPointF(4.5, 12.5),
                QPointF(3, 6.5)}));
        break;
    case AppMenuIcon::Map:
        painter.drawLine(QPointF(4, 4), QPointF(4, 12));
        painter.drawLine(QPointF(8, 3), QPointF(8, 11));
        painter.drawLine(QPointF(12, 4), QPointF(12, 12));
        painter.drawPolyline(QPolygonF(
            QVector<QPointF>{
                QPointF(4, 4),
                QPointF(8, 3),
                QPointF(12, 4),
                QPointF(12, 12),
                QPointF(8, 11),
                QPointF(4, 12),
                QPointF(4, 4)}));
        break;
    case AppMenuIcon::Manage:
        painter.drawRoundedRect(QRectF(3, 4, 10, 8), 1.5, 1.5);
        painter.drawLine(QPointF(6, 4), QPointF(6, 12));
        painter.drawLine(QPointF(10, 4), QPointF(10, 12));
        break;
    case AppMenuIcon::Save:
        painter.drawRoundedRect(QRectF(3.5, 3, 9, 10), 1.2, 1.2);
        painter.drawLine(QPointF(5, 4.5), QPointF(10.5, 4.5));
        painter.drawRect(QRectF(5, 9, 6, 3));
        break;
    case AppMenuIcon::Import:
        drawTray();
        painter.drawLine(QPointF(8, 3), QPointF(8, 9));
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(5.5, 6.8), QPointF(8, 9.2), QPointF(10.5, 6.8)}));
        break;
    case AppMenuIcon::Export:
        drawTray();
        painter.drawLine(QPointF(8, 9), QPointF(8, 3));
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(5.5, 5.2), QPointF(8, 2.8), QPointF(10.5, 5.2)}));
        break;
    case AppMenuIcon::Run:
        drawPlay();
        break;
    case AppMenuIcon::Script:
        drawDocument();
        painter.drawText(QRectF(5, 4, 7, 8), Qt::AlignCenter, QStringLiteral(">"));
        break;
    case AppMenuIcon::Commit:
        painter.drawEllipse(QRectF(4, 3, 8, 3));
        painter.drawLine(QPointF(4, 4.5), QPointF(4, 11.5));
        painter.drawLine(QPointF(12, 4.5), QPointF(12, 11.5));
        painter.drawEllipse(QRectF(4, 10, 8, 3));
        painter.drawLine(QPointF(6, 7.5), QPointF(7.5, 9));
        painter.drawLine(QPointF(7.5, 9), QPointF(10.5, 6));
        break;
    case AppMenuIcon::Settings:
        painter.drawEllipse(QPointF(8, 8), 2.3, 2.3);
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * pi / 4.0;
            painter.drawLine(
                QPointF(8 + std::cos(a) * 4.1, 8 + std::sin(a) * 4.1),
                QPointF(8 + std::cos(a) * 5.2, 8 + std::sin(a) * 5.2));
        }
        break;
    case AppMenuIcon::Quit:
        painter.drawArc(QRectF(4, 4, 8, 8), 35 * 16, 290 * 16);
        painter.drawLine(QPointF(8, 3), QPointF(8, 8));
        break;
    case AppMenuIcon::NavigateBack:
        painter.drawLine(QPointF(4, 8), QPointF(12, 8));
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(7, 5), QPointF(4, 8), QPointF(7, 11)}));
        break;
    case AppMenuIcon::NavigateForward:
        painter.drawLine(QPointF(4, 8), QPointF(12, 8));
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(9, 5), QPointF(12, 8), QPointF(9, 11)}));
        break;
    case AppMenuIcon::Undo:
        painter.drawArc(QRectF(4, 4, 8, 8), 40 * 16, 250 * 16);
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(4.5, 6), QPointF(3.5, 9), QPointF(6.5, 8)}));
        break;
    case AppMenuIcon::Redo:
        painter.drawArc(QRectF(4, 4, 8, 8), -110 * 16, 250 * 16);
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(11.5, 6), QPointF(12.5, 9), QPointF(9.5, 8)}));
        break;
    case AppMenuIcon::Patch:
        painter.drawLine(QPointF(4, 12), QPointF(12, 4));
        painter.drawLine(QPointF(10, 4), QPointF(12, 6));
        painter.drawLine(QPointF(4, 12), QPointF(7, 11));
        break;
    case AppMenuIcon::IOMode:
        painter.drawRoundedRect(QRectF(4, 4, 8, 8), 1.5, 1.5);
        painter.drawLine(QPointF(6, 3), QPointF(6, 5));
        painter.drawLine(QPointF(10, 3), QPointF(10, 5));
        painter.drawLine(QPointF(8, 12), QPointF(8, 14));
        break;
    case AppMenuIcon::Highlight:
        painter.drawRoundedRect(QRectF(4, 4, 8, 8), 1.5, 1.5);
        painter.drawLine(QPointF(5.5, 8), QPointF(10.5, 8));
        painter.drawLine(QPointF(8, 5.5), QPointF(8, 10.5));
        break;
    case AppMenuIcon::View:
        painter.drawEllipse(QRectF(3, 5, 10, 6));
        painter.drawEllipse(QPointF(8, 8), 1.8, 1.8);
        break;
    case AppMenuIcon::Info:
        painter.drawLine(QPointF(4, 5), QPointF(12, 5));
        painter.drawLine(QPointF(4, 8), QPointF(12, 8));
        painter.drawLine(QPointF(4, 11), QPointF(10, 11));
        break;
    case AppMenuIcon::Storage:
        painter.drawEllipse(QRectF(4, 3, 8, 3));
        painter.drawLine(QPointF(4, 4.5), QPointF(4, 11.5));
        painter.drawLine(QPointF(12, 4.5), QPointF(12, 11.5));
        painter.drawEllipse(QRectF(4, 10, 8, 3));
        break;
    case AppMenuIcon::Extra:
        painter.drawRoundedRect(QRectF(3.5, 3.5, 9, 9), 1.5, 1.5);
        painter.drawLine(QPointF(8, 5.5), QPointF(8, 10.5));
        painter.drawLine(QPointF(5.5, 8), QPointF(10.5, 8));
        break;
    case AppMenuIcon::ZoomIn:
    case AppMenuIcon::ZoomOut:
    case AppMenuIcon::ZoomReset:
        drawMagnifier();
        if (icon == AppMenuIcon::ZoomReset) {
            painter.drawLine(QPointF(5.7, 8), QPointF(10, 8));
        } else {
            painter.drawLine(QPointF(5.7, 7), QPointF(9.3, 7));
            if (icon == AppMenuIcon::ZoomIn) {
                painter.drawLine(QPointF(7.5, 5.2), QPointF(7.5, 8.8));
            }
        }
        break;
    case AppMenuIcon::Analysis:
        drawMagnifier();
        break;
    case AppMenuIcon::Function:
        painter.drawText(QRectF(3, 2, 10, 12), Qt::AlignCenter, QStringLiteral("f"));
        break;
    case AppMenuIcon::Autoname:
        painter.drawPolyline(QPolygonF(
            {QPointF(4, 5),
             QPointF(8, 3),
             QPointF(13, 8),
             QPointF(8, 13),
             QPointF(3, 8),
             QPointF(4, 5)}));
        painter.drawEllipse(QPointF(7, 6.5), 0.8, 0.8);
        break;
    case AppMenuIcon::Plugin:
        painter.drawRoundedRect(QRectF(4, 3.5, 8, 9), 1.5, 1.5);
        painter.drawLine(QPointF(8, 3.5), QPointF(8, 12.5));
        painter.drawLine(QPointF(4, 8), QPointF(12, 8));
        break;
    case AppMenuIcon::Package:
        painter.drawPolyline(QPolygonF(
            {QPointF(4, 5),
             QPointF(8, 3),
             QPointF(12, 5),
             QPointF(12, 11),
             QPointF(8, 13),
             QPointF(4, 11),
             QPointF(4, 5)}));
        painter.drawLine(QPointF(4, 5), QPointF(8, 7));
        painter.drawLine(QPointF(12, 5), QPointF(8, 7));
        painter.drawLine(QPointF(8, 7), QPointF(8, 13));
        break;
    case AppMenuIcon::Calculator:
        painter.drawRoundedRect(QRectF(4, 3, 8, 10), 1.5, 1.5);
        painter.drawLine(QPointF(5.5, 6), QPointF(10.5, 6));
        painter.drawPoint(QPointF(6, 8.5));
        painter.drawPoint(QPointF(8, 8.5));
        painter.drawPoint(QPointF(10, 8.5));
        painter.drawPoint(QPointF(6, 10.5));
        painter.drawPoint(QPointF(8, 10.5));
        painter.drawPoint(QPointF(10, 10.5));
        break;
    case AppMenuIcon::Syscalls:
        painter.drawText(QRectF(2, 2, 12, 12), Qt::AlignCenter, QStringLiteral("#"));
        break;
    case AppMenuIcon::Assembler:
        painter.drawText(QRectF(1, 2, 14, 12), Qt::AlignCenter, QStringLiteral("asm"));
        break;
    case AppMenuIcon::Web:
        painter.drawArc(QRectF(3.5, 5, 5, 5), 75 * 16, 210 * 16);
        painter.drawArc(QRectF(6.5, 4, 6, 7), 10 * 16, 230 * 16);
        painter.drawLine(QPointF(5, 11), QPointF(12, 11));
        break;
    case AppMenuIcon::Layout:
        painter.drawRect(QRectF(3, 3, 10, 10));
        painter.drawLine(QPointF(3, 7), QPointF(13, 7));
        painter.drawLine(QPointF(7, 7), QPointF(7, 13));
        break;
    case AppMenuIcon::Refresh:
        painter.drawArc(QRectF(4, 4, 8, 8), 40 * 16, 280 * 16);
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(10.5, 3.8), QPointF(13, 5), QPointF(11.5, 7)}));
        break;
    case AppMenuIcon::Lock:
        painter.drawRoundedRect(QRectF(4, 7, 8, 6), 1.3, 1.3);
        painter.drawArc(QRectF(5, 3, 6, 7), 0, 180 * 16);
        break;
    case AppMenuIcon::Reset:
        painter.drawArc(QRectF(4, 4, 8, 8), 60 * 16, 260 * 16);
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(4.5, 5.2), QPointF(4, 8), QPointF(6.8, 7.2)}));
        painter.drawLine(QPointF(6, 10), QPointF(10, 6));
        break;
    case AppMenuIcon::Issue:
        painter.drawRoundedRect(QRectF(4, 4, 8, 8), 3, 3);
        painter.drawLine(QPointF(6, 6), QPointF(10, 10));
        painter.drawLine(QPointF(10, 6), QPointF(6, 10));
        break;
    case AppMenuIcon::Website:
        painter.drawEllipse(QRectF(3, 3, 10, 10));
        painter.drawLine(QPointF(3, 8), QPointF(13, 8));
        painter.drawArc(QRectF(5, 3, 6, 10), 90 * 16, 180 * 16);
        painter.drawArc(QRectF(5, 3, 6, 10), -90 * 16, 180 * 16);
        break;
    case AppMenuIcon::Book:
        painter.drawPolyline(QPolygonF(
            QVector<QPointF>{
                QPointF(3.5, 4),
                QPointF(7.5, 5),
                QPointF(7.5, 13),
                QPointF(3.5, 12),
                QPointF(3.5, 4)}));
        painter.drawPolyline(QPolygonF(
            QVector<QPointF>{
                QPointF(12.5, 4),
                QPointF(8.5, 5),
                QPointF(8.5, 13),
                QPointF(12.5, 12),
                QPointF(12.5, 4)}));
        break;
    case AppMenuIcon::Fortune:
        painter.drawPolyline(QPolygonF(
            QVector<QPointF>{
                QPointF(8, 3),
                QPointF(9.5, 6.5),
                QPointF(13, 8),
                QPointF(9.5, 9.5),
                QPointF(8, 13),
                QPointF(6.5, 9.5),
                QPointF(3, 8),
                QPointF(6.5, 6.5),
                QPointF(8, 3)}));
        break;
    case AppMenuIcon::Update:
        painter.drawLine(QPointF(8, 3), QPointF(8, 10));
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(5.5, 7.5), QPointF(8, 10), QPointF(10.5, 7.5)}));
        painter.drawLine(QPointF(5, 13), QPointF(11, 13));
        break;
    case AppMenuIcon::About:
        painter.drawEllipse(QRectF(3, 3, 10, 10));
        painter.drawLine(QPointF(8, 7), QPointF(8, 11));
        painter.drawPoint(QPointF(8, 5.3));
        break;
    case AppMenuIcon::Debug:
        painter.drawLine(QPointF(8, 3), QPointF(8, 13));
        painter.drawLine(QPointF(4, 5), QPointF(12, 5));
        painter.drawLine(QPointF(4, 11), QPointF(12, 11));
        painter.drawRoundedRect(QRectF(5, 4, 6, 8), 1.5, 1.5);
        break;
    case AppMenuIcon::Profile:
        drawDocument();
        painter.drawLine(QPointF(6, 7), QPointF(10, 7));
        painter.drawLine(QPointF(6, 9.5), QPointF(9, 9.5));
        break;
    case AppMenuIcon::Search:
        drawMagnifier();
        break;
    case AppMenuIcon::Xrefs:
        painter.drawEllipse(QPointF(5, 8), 2.0, 2.0);
        painter.drawEllipse(QPointF(11, 5), 2.0, 2.0);
        painter.drawEllipse(QPointF(11, 11), 2.0, 2.0);
        painter.drawLine(QPointF(6.7, 7), QPointF(9.3, 5.8));
        painter.drawLine(QPointF(6.7, 9), QPointF(9.3, 10.2));
        break;
    case AppMenuIcon::Refs:
        painter.drawLine(QPointF(4, 4), QPointF(12, 4));
        painter.drawLine(QPointF(4, 8), QPointF(10, 8));
        painter.drawLine(QPointF(4, 12), QPointF(12, 12));
        painter.drawLine(QPointF(10, 6), QPointF(12, 8));
        painter.drawLine(QPointF(12, 8), QPointF(10, 10));
        break;
    case AppMenuIcon::Console:
        painter.drawRoundedRect(QRectF(3, 4, 10, 8), 1.5, 1.5);
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(5, 6), QPointF(7, 8), QPointF(5, 10)}));
        painter.drawLine(QPointF(8, 10), QPointF(11, 10));
        break;
    case AppMenuIcon::Dashboard:
        painter.drawRect(QRectF(3, 3, 4, 4));
        painter.drawRect(QRectF(9, 3, 4, 4));
        painter.drawRect(QRectF(3, 9, 4, 4));
        painter.drawRect(QRectF(9, 9, 4, 4));
        break;
    case AppMenuIcon::Graph:
        painter.drawEllipse(QPointF(5, 5), 1.8, 1.8);
        painter.drawEllipse(QPointF(11, 6.5), 1.8, 1.8);
        painter.drawEllipse(QPointF(7, 12), 1.8, 1.8);
        painter.drawLine(QPointF(6.6, 5.4), QPointF(9.4, 6.1));
        painter.drawLine(QPointF(10.1, 8), QPointF(8, 10.5));
        painter.drawLine(QPointF(5.5, 6.7), QPointF(6.4, 10.2));
        break;
    case AppMenuIcon::Disassembly:
        painter.drawText(QRectF(2, 2, 12, 12), Qt::AlignCenter, QStringLiteral("pd"));
        break;
    case AppMenuIcon::Hexdump:
        painter.drawText(QRectF(2, 2, 12, 12), Qt::AlignCenter, QStringLiteral("xx"));
        break;
    case AppMenuIcon::Decompiler:
        painter.drawText(QRectF(2, 2, 12, 12), Qt::AlignCenter, QStringLiteral("C"));
        break;
    case AppMenuIcon::CustomCommand:
        painter.drawText(QRectF(2, 2, 12, 12), Qt::AlignCenter, QStringLiteral(":"));
        break;
    }

    return QIcon(pixmap);
}

void setAppMenuIcon(const QWidget *widget, QAction *action, AppMenuIcon icon, const QColor &color)
{
    if (action) {
        action->setIcon(makeAppMenuIcon(widget, icon, color));
    }
}

void setAppMenuIcon(const QWidget *widget, QMenu *menu, AppMenuIcon icon, const QColor &color)
{
    if (menu) {
        setAppMenuIcon(widget, menu->menuAction(), icon, color);
    }
}

QIcon makeBlankAppMenuIcon(const QWidget *widget)
{
    const qreal ratio = widget ? qhelpers::devicePixelRatio(widget) : qreal(1.0);
    const int size = 16;
    QPixmap pixmap(size * ratio, size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    return QIcon(pixmap);
}

void fillMenuIconGaps(const QWidget *widget, QMenu *menu, QSet<QMenu *> &visited)
{
    if (!menu || visited.contains(menu)) {
        return;
    }
    visited.insert(menu);

    const QIcon blankIcon = makeBlankAppMenuIcon(widget);
    for (QAction *action : menu->actions()) {
        if (!action || action->isSeparator()) {
            continue;
        }
        if (action->icon().isNull()) {
            action->setIcon(blankIcon);
            action->setIconVisibleInMenu(true);
        }
        if (QMenu *submenu = action->menu()) {
            fillMenuIconGaps(widget, submenu, visited);
        }
    }
}

QString r2QuotedFileArg(const QString &path)
{
    QString result = IaitoCore::sanitizeStringForCommand(path);
    result.replace(QLatin1Char('"'), QLatin1Char('_'));
    result.replace(QLatin1Char('\n'), QLatin1Char('_'));
    result.replace(QLatin1Char('\r'), QLatin1Char('_'));
    return QStringLiteral("\"%1\"").arg(result);
}

QString buildAnalyzePluginStatusText(
    const QString &name, const QString &args, const QString &description)
{
    if (!args.isEmpty() && !description.isEmpty()) {
        return QStringLiteral("%1 %2 - %3").arg(name, args, description);
    }
    if (!args.isEmpty()) {
        return QStringLiteral("%1 %2").arg(name, args);
    }
    if (!description.isEmpty()) {
        return QStringLiteral("%1: %2").arg(name, description);
    }
    return name;
}

void mergeAnalyzePluginCommandEntry(
    AnalyzePluginCommandEntry &entry, const QString &description, const QString &args)
{
    if (!args.isEmpty() && entry.args.isEmpty()) {
        entry.args = args;
    }
    const QString trimmedDescription = description.trimmed();
    if (trimmedDescription.isEmpty()) {
        return;
    }
    if (entry.description.isEmpty()) {
        entry.description = trimmedDescription;
        return;
    }
    if (!entry.description.contains(trimmedDescription)) {
        entry.description += QStringLiteral("; ") + trimmedDescription;
    }
}

void mergeAnalyzePluginCommandEntry(
    QMap<QString, AnalyzePluginCommandEntry> &entriesByName, AnalyzePluginCommandEntry entry)
{
    auto existing = entriesByName.find(entry.name);
    if (existing == entriesByName.end()) {
        entriesByName.insert(entry.name, std::move(entry));
        return;
    }
    mergeAnalyzePluginCommandEntry(existing.value(), entry.description, entry.args);
}

bool parseAnalyzePluginLine(const QString &line, AnalyzePluginCommandEntry &entry)
{
    entry = {};

    QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    if (trimmed.startsWith(QLatin1Char('|'))) {
        trimmed = trimmed.mid(1).trimmed();
    } else if (trimmed.startsWith(QStringLiteral("Usage:"))) {
        trimmed = trimmed.mid(QStringLiteral("Usage:").size()).trimmed();
    }
    if (!trimmed.startsWith(QLatin1Char('a'))) {
        return false;
    }

    int nameEnd = 0;
    while (nameEnd < trimmed.size() && !trimmed.at(nameEnd).isSpace()
           && trimmed.at(nameEnd) != QLatin1Char('[') && trimmed.at(nameEnd) != QLatin1Char('<')) {
        nameEnd++;
    }
    if (nameEnd <= 0) {
        return false;
    }

    entry.name = trimmed.left(nameEnd);
    QString rest = trimmed.mid(nameEnd);
    int descriptionStart = -1;
    for (int i = 0; i + 1 < rest.size(); ++i) {
        if (rest.at(i).isSpace() && rest.at(i + 1).isSpace()) {
            descriptionStart = i;
            break;
        }
    }

    QString argsText = descriptionStart >= 0 ? rest.left(descriptionStart).trimmed()
                                             : rest.trimmed();
    const QString descriptionText = descriptionStart >= 0 ? rest.mid(descriptionStart).trimmed()
                                                          : QString();
    QStringList argParts;
    while (!argsText.isEmpty()
           && (argsText.startsWith(QLatin1Char('[')) || argsText.startsWith(QLatin1Char('<')))) {
        const QChar open = argsText.at(0);
        const QChar close = open == QLatin1Char('[') ? QLatin1Char(']') : QLatin1Char('>');
        const int closeIndex = argsText.indexOf(close, 1);
        if (closeIndex < 0) {
            break;
        }
        argParts.append(argsText.left(closeIndex + 1));
        argsText = argsText.mid(closeIndex + 1).trimmed();
    }
    entry.args = argParts.join(QStringLiteral(" "));
    entry.description = descriptionText;
    return !entry.name.isEmpty();
}

QList<AnalyzePluginPlaceholder> extractAnalyzePluginPlaceholders(const QString &args)
{
    QList<AnalyzePluginPlaceholder> placeholders;
    int pos = 0;
    while (pos < args.size()) {
        while (pos < args.size() && args.at(pos).isSpace()) {
            pos++;
        }
        if (pos >= args.size()) {
            break;
        }
        const QChar open = args.at(pos);
        if (open != QLatin1Char('[') && open != QLatin1Char('<')) {
            break;
        }
        const QChar close = open == QLatin1Char('[') ? QLatin1Char(']') : QLatin1Char('>');
        const int closeIndex = args.indexOf(close, pos + 1);
        if (closeIndex < 0) {
            break;
        }
        placeholders.append(
            {args.mid(pos + 1, closeIndex - pos - 1).trimmed(), open == QLatin1Char('[')});
        pos = closeIndex + 1;
    }
    return placeholders;
}

} // namespace

static Qt::ToolBarArea toolBarAreaFromLocation(Configuration::VisualNavbarLocation location)
{
    switch (location) {
    case Configuration::VisualNavbarLocation::Top:
    case Configuration::VisualNavbarLocation::SuperTop:
        return Qt::TopToolBarArea;
    case Configuration::VisualNavbarLocation::Bottom:
    case Configuration::VisualNavbarLocation::SuperBottom:
        return Qt::BottomToolBarArea;
    case Configuration::VisualNavbarLocation::Left:
        return Qt::LeftToolBarArea;
    case Configuration::VisualNavbarLocation::Right:
        return Qt::RightToolBarArea;
    }
    return Qt::TopToolBarArea;
}

void MainWindow::updateStatusBar(RVA addr, const QString &name)
{
    QString msg = RAddressString(addr);
    if (!name.isEmpty()) {
        msg += " " + name;
    }
    statusBar()->showMessage(msg);
}

using namespace Iaito;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , core(Core())
    , ui(new Ui::MainWindow)
{
    tabsOnTop = true; // default option
    configuration = Config();

    initUI();
}

MainWindow::~MainWindow() {}

void MainWindow::initUI()
{
    ui->setupUi(this);
    // always show the main window status bar
    statusBar()->show();
    // Install event filter to catch function key releases
    qApp->installEventFilter(this);

    // Initialize context menu extensions for plugins
    disassemblyContextMenuExtensions = new QMenu(tr("Plugins"), this);
    addressableContextMenuExtensions = new QMenu(tr("Plugins"), this); // XXX both plugins?

    connect(ui->actionExtraDecompiler, &QAction::triggered, this, &MainWindow::addExtraDecompiler);
    connect(ui->actionExtraGraph, &QAction::triggered, this, &MainWindow::addExtraGraph);
    connect(ui->actionExtraDisassembly, &QAction::triggered, this, &MainWindow::addExtraDisassembly);
    connect(ui->actionExtraHexdump, &QAction::triggered, this, &MainWindow::addExtraHexdump);
    connect(ui->actionAddCustomCommand, &QAction::triggered, this, &MainWindow::addExtraCustomCommand);
    QAction *calculatorAction = new QAction(tr("Calculator"), this);
    setAppMenuIcon(this, calculatorAction, AppMenuIcon::Calculator, QColor(191, 128, 32));
    calculatorAction->setToolTip(tr("Open the rax2 calculator shell"));
    calculatorAction->setStatusTip(tr("Evaluate rax2 expressions and keep a calculator history"));
    ui->menuTools->insertAction(ui->actionStart_Web_Server, calculatorAction);
    connect(calculatorAction, &QAction::triggered, this, [this]() {
        if (auto *widget = findChild<CalculatorWidget *>()) {
            widget->show();
            widget->raiseMemoryWidget();
            return;
        }

        auto *widget = new CalculatorWidget(this);
        widget->resize(560, 360);
        addExtraWidget(widget);
        widget->setFloating(true);
        widget->raiseMemoryWidget();
    });
    QAction *syscallsAction = new QAction(tr("Syscalls"), this);
    setAppMenuIcon(this, syscallsAction, AppMenuIcon::Syscalls, QColor(0, 137, 190));
    syscallsAction->setToolTip(tr("Browse syscalls for the current analysis settings"));
    syscallsAction->setStatusTip(
        tr("List syscalls and inspect r2 as output for a selected syscall number"));
    ui->menuTools->insertAction(ui->actionStart_Web_Server, syscallsAction);
    connect(syscallsAction, &QAction::triggered, this, [this]() {
        if (auto *widget = findChild<SyscallsWidget *>()) {
            widget->show();
            widget->raiseMemoryWidget();
            return;
        }

        auto *widget = new SyscallsWidget(this);
        widget->resize(760, 520);
        addExtraWidget(widget);
        widget->setFloating(true);
        widget->raiseMemoryWidget();
    });
    QAction *assemblerAction = new QAction(tr("Assembler..."), this);
    setAppMenuIcon(this, assemblerAction, AppMenuIcon::Assembler, QColor(216, 88, 64));
    assemblerAction->setToolTip(tr("Assemble and disassemble bytes at an address"));
    assemblerAction->setStatusTip(
        tr("Assemble instructions into bytes, or disassemble edited bytes"));
    ui->menuTools->insertAction(ui->actionStart_Web_Server, assemblerAction);
    connect(assemblerAction, &QAction::triggered, this, [this]() {
        if (auto *dialog = findChild<AssemblerDialog *>()) {
            dialog->show();
            dialog->raise();
            dialog->activateWindow();
            return;
        }

        auto *dialog = new AssemblerDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    });
    connect(ui->actionCommitChanges, &QAction::triggered, this, []() {
        Core()->commitWriteCache();
    });
    ui->actionCommitChanges->setEnabled(false);
    connect(Core(), &IaitoCore::ioCacheChanged, ui->actionCommitChanges, &QAction::setEnabled);
    connect(ui->actionUndoWrite, &QAction::triggered, this, [this]() {
        core->cmdRaw("wcu");
        emit core->refreshCodeViews();
        updateWriteUndoRedoActions();
    });
    connect(ui->actionRedoWrite, &QAction::triggered, this, [this]() {
        core->cmdRaw("wcU");
        emit core->refreshCodeViews();
        updateWriteUndoRedoActions();
    });
    updateWriteUndoRedoActions();

    widgetTypeToConstructorMap.insert(GraphWidget::getWidgetType(), getNewInstance<GraphWidget>);
    widgetTypeToConstructorMap
        .insert(DisassemblyWidget::getWidgetType(), getNewInstance<DisassemblyWidget>);
    widgetTypeToConstructorMap.insert(HexdumpWidget::getWidgetType(), getNewInstance<HexdumpWidget>);
    widgetTypeToConstructorMap
        .insert(DecompilerWidget::getWidgetType(), getNewInstance<DecompilerWidget>);

    initToolBar();
    initDocks();

    emptyState = saveState();
    /*
     *  Some global shortcuts
     */

    // Period goes to command entry
    QShortcut *cmd_shortcut = new QShortcut(QKeySequence(Qt::Key_Period), this);
    connect(cmd_shortcut, &QShortcut::activated, consoleDock, &ConsoleWidget::focusInputLineEdit);

    // G and S goes to goto entry
    QShortcut *goto_shortcut = new QShortcut(QKeySequence(Qt::Key_G), this);
    connect(goto_shortcut, &QShortcut::activated, this->omnibar, [this]() {
        this->omnibar->setFocus();
    });
    QShortcut *seek_shortcut = new QShortcut(QKeySequence(Qt::Key_S), this);
    connect(seek_shortcut, &QShortcut::activated, this->omnibar, [this]() {
        this->omnibar->setFocus();
    });
    QShortcut *seek_to_func_end_shortcut = new QShortcut(QKeySequence(Qt::Key_Dollar), this);
    connect(
        seek_to_func_end_shortcut,
        &QShortcut::activated,
        this,
        &MainWindow::seekToFunctionLastInstruction);
    QShortcut *seek_to_func_start_shortcut = new QShortcut(QKeySequence(Qt::Key_AsciiCircum), this);
    connect(
        seek_to_func_start_shortcut, &QShortcut::activated, this, &MainWindow::seekToFunctionStart);

    QShortcut *refresh_shortcut = new QShortcut(QKeySequence(QKeySequence::Refresh), this);
    connect(refresh_shortcut, &QShortcut::activated, this, &MainWindow::refreshAll);

    connect(ui->actionZoomIn, &QAction::triggered, this, &MainWindow::onZoomIn);
    connect(ui->actionZoomOut, &QAction::triggered, this, &MainWindow::onZoomOut);
    connect(ui->actionZoomReset, &QAction::triggered, this, &MainWindow::onZoomReset);

    connect(core, &IaitoCore::projectSaved, this, &MainWindow::projectSaved);
    connect(core, &IaitoCore::toggleDebugView, this, &MainWindow::toggleDebugView);
    connect(core, &IaitoCore::newMessage, this->consoleDock, &ConsoleWidget::addOutput);
    connect(core, &IaitoCore::newDebugMessage, this->consoleDock, &ConsoleWidget::addDebugOutput);
    connect(
        core,
        &IaitoCore::showMemoryWidgetRequested,
        this,
        static_cast<void (MainWindow::*)()>(&MainWindow::showMemoryWidget));

    updateTasksIndicator();
    connect(
        core->getAsyncTaskManager(),
        &AsyncTaskManager::tasksChanged,
        this,
        &MainWindow::updateTasksIndicator);

    // Undo and redo seek
    ui->actionBackward->setShortcut(QKeySequence::Back);
    ui->actionForward->setShortcut(QKeySequence::Forward);

    initBackForwardMenu();

    connect(core, &IaitoCore::ioModeChanged, this, &MainWindow::setAvailableIOModeOptions);

    QActionGroup *ioModeActionGroup = new QActionGroup(this);

    ioModeActionGroup->addAction(ui->actionCacheMode);
    ioModeActionGroup->addAction(ui->actionWriteMode);
    ioModeActionGroup->addAction(ui->actionReadOnly);

    connect(ui->actionCacheMode, &QAction::triggered, this, [this]() {
        ioModesController.setIOMode(IOModesController::Mode::CACHE);
        setAvailableIOModeOptions();
    });

    connect(ui->actionWriteMode, &QAction::triggered, this, [this]() {
        ioModesController.setIOMode(IOModesController::Mode::WRITE);
        setAvailableIOModeOptions();
    });

    connect(ui->actionReadOnly, &QAction::triggered, this, [this]() {
        ioModesController.setIOMode(IOModesController::Mode::READ_ONLY);
        setAvailableIOModeOptions();
    });

    connect(ui->actionSaveLayout, &QAction::triggered, this, &MainWindow::saveNamedLayout);
    connect(ui->actionManageLayouts, &QAction::triggered, this, &MainWindow::manageLayouts);
    connect(ui->actionWebsite, &QAction::triggered, this, &MainWindow::websiteClicked);
    connect(ui->actionFortune, &QAction::triggered, this, &MainWindow::fortuneClicked);
    connect(ui->actionBook, &QAction::triggered, this, &MainWindow::bookClicked);
    connect(ui->actionCheckForUpdates, &QAction::triggered, this, &MainWindow::checkForUpdatesClicked);

    /* Setup plugins interfaces */
    const auto &plugins = Plugins()->getPlugins();
    for (auto &plugin : plugins) {
        plugin->setupInterface(this);
    }
    // Add r2ai dock widget as plugin
    {
        R2AIWidget *r2aiDock = new R2AIWidget(this);
        addPluginDockWidget(r2aiDock);
    }

    // Check if plugins are loaded and display tooltips accordingly
    ui->menuWindows->setToolTipsVisible(true);
    ui->menuEdit->setToolTipsVisible(true);
    ui->menuPlugins->setToolTipsVisible(true);
    // Only disable the Plugins menu when no IAito plugins AND no dock widgets added
    bool hasIaitoPlugins = !plugins.empty();
    bool hasDockEntries = !ui->menuPlugins->actions().isEmpty();
    if (!hasIaitoPlugins && !hasDockEntries) {
        ui->menuPlugins->menuAction()->setToolTip(
            tr("No plugins installed. Check the plugins section on Iaito "
               "documentation to learn more."));
        ui->menuPlugins->setEnabled(false);
    } else if (!hasDockEntries) {
        ui->menuPlugins->menuAction()->setToolTip(
            tr("The installed plugins didn't add entries to this menu."));
        ui->menuPlugins->setEnabled(false);
    }

    connect(ui->actionUnlock, &QAction::toggled, this, [this](bool unlock) { lockDocks(!unlock); });

#if QT_VERSION < QT_VERSION_CHECK(5, 7, 0)
    ui->actionGrouped_dock_dragging->setVisible(false);
#endif

    enableDebugWidgetsMenu(false);
    readSettings();

    // Display tooltip for the Analyze Program action
    ui->actionAnalyze->setToolTip("Run the analysis");
    ui->menuPlugins->menuAction()->setToolTip(
        tr("Browse plugin panels and analysis plugin commands"));
    ui->menuPlugins->menuAction()->setStatusTip(
        tr("Browse plugin panels and analysis plugin commands"));
    ui->menuFile->setToolTipsVisible(true);

    auto connectMenuStatusTips = [this](QMenu *menu) {
        connect(menu, &QMenu::hovered, this, [this](QAction *action) {
            if (action && !action->isSeparator() && !action->statusTip().isEmpty()) {
                statusBar()->showMessage(action->statusTip());
                return;
            }
            updateStatusBar(core->getOffset());
        });
        connect(menu, &QMenu::aboutToHide, this, [this]() { updateStatusBar(core->getOffset()); });
    };

    connect(ui->menuPlugins, &QMenu::aboutToShow, this, &MainWindow::rebuildAnalyzePluginsMenu);
    connect(ui->menuRun, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentScriptsMenu);
    connect(ui->menuFile, &QMenu::aboutToShow, this, &MainWindow::updateSaveProjectAction);
    connect(ui->menuEdit, &QMenu::aboutToShow, this, &MainWindow::updateWriteUndoRedoActions);
    connect(core, &IaitoCore::ioCacheChanged, this, &MainWindow::updateWriteUndoRedoActions);
    connect(core, &IaitoCore::refreshCodeViews, this, &MainWindow::updateWriteUndoRedoActions);
    connect(core, &IaitoCore::refreshAll, this, &MainWindow::updateWriteUndoRedoActions);
    connectMenuStatusTips(ui->menuFile);
    connectMenuStatusTips(ui->menuEdit);
    connectMenuStatusTips(ui->menuCode);
    connectMenuStatusTips(ui->menuTools);
    connectMenuStatusTips(ui->menuPlugins);

    updateSaveProjectAction();
    applyTopLevelMenuIcons();
    fillTopLevelMenuIconGaps();
}

void MainWindow::initToolBar()
{
    chooseThemeIcons();

    ui->mainToolBar->addSeparator();

    QToolButton *editBtn = new QToolButton(ui->mainToolBar);
    editBtn->setIcon(QIcon(QStringLiteral(":/img/icons/pencil_thin.svg")));
    editBtn->setToolTip(tr("Edit (rename, flags, comment, write)"));
    editBtn->setPopupMode(QToolButton::InstantPopup);
    editBtn->setObjectName(QStringLiteral("toolbarEditButton"));
    QMenu *editMenu = new QMenu(editBtn);
    editMenu->addAction(tr("Rename function..."), this, [this]() {
        const RVA off = Core()->getOffset();
        RAnalFunction *fcn = Core()->functionIn(off);
        if (!fcn) {
            QMessageBox::information(
                this, tr("Rename function"), tr("No function at current offset."));
            return;
        }
        bool ok = false;
        QString cur = QString::fromUtf8(fcn->name);
        QString newName = QInputDialog::getText(
            this,
            tr("Rename function %1").arg(cur),
            tr("Function name:"),
            QLineEdit::Normal,
            cur,
            &ok);
        if (ok && !newName.isEmpty()) {
            Core()->renameFunction(fcn->addr, newName);
        }
    });
    editMenu->addAction(tr("Rename / set flag..."), this, [this]() {
        FlagDialog dialog(Core()->getOffset(), 1, this);
        dialog.exec();
    });
    editMenu->addSeparator();
    editMenu->addAction(tr("Delete function"), this, [this]() {
        const RVA off = Core()->getOffset();
        RAnalFunction *fcn = Core()->functionIn(off);
        if (!fcn) {
            QMessageBox::information(
                this, tr("Delete function"), tr("No function at current offset."));
            return;
        }
        Core()->delFunction(fcn->addr);
    });
    editMenu->addAction(tr("Delete flag"), this, []() { Core()->delFlag(Core()->getOffset()); });
    editMenu->addSeparator();
    editMenu->addAction(tr("Add / edit comment..."), this, [this]() {
        CommentsDialog::addOrEditComment(Core()->getOffset(), this);
    });
    editMenu->addSeparator();
    editMenu->addAction(ui->actionUndoWrite);
    editMenu->addAction(ui->actionRedoWrite);
    editMenu->addSeparator();
    editMenu->addAction(ui->actionPatchInstruction);
    editMenu->addAction(ui->actionPatchString);
    editMenu->addAction(ui->actionPatchBytes);
    editBtn->setMenu(editMenu);
    ui->mainToolBar->addWidget(editBtn);

    QAction *actXrefs
        = new QAction(QIcon(QStringLiteral(":/img/icons/target.svg")), tr("X-Refs"), this);
    actXrefs->setToolTip(tr("Show X-Refs for current offset"));
    actXrefs->setObjectName(QStringLiteral("actionToolbarXrefs"));
    connect(actXrefs, &QAction::triggered, this, [this]() {
        const RVA off = Core()->getOffset();
        XrefsDialog dialog(this, nullptr);
        dialog.fillRefsForAddress(off, RAddressString(off), false);
        dialog.exec();
    });
    ui->mainToolBar->addAction(actXrefs);

    QAction *actCode
        = new QAction(QIcon(QStringLiteral(":/img/icons/disas.svg")), tr("Disassembly"), this);
    actCode->setToolTip(tr("Show Disassembly view at current offset"));
    actCode->setShortcut(QKeySequence(Qt::Key_C));
    actCode->setObjectName(QStringLiteral("actionToolbarDisassembly"));
    connect(actCode, &QAction::triggered, this, [this]() {
        showMemoryWidget(MemoryWidgetType::Disassembly);
    });
    ui->mainToolBar->addAction(actCode);

    QAction *actGraph
        = new QAction(QIcon(QStringLiteral(":/img/icons/graph.svg")), tr("Graph"), this);
    actGraph->setToolTip(tr("Show Graph view at current offset"));
    actGraph->setShortcut(QKeySequence(Qt::Key_G));
    actGraph->setObjectName(QStringLiteral("actionToolbarGraph"));
    connect(actGraph, &QAction::triggered, this, [this]() {
        showMemoryWidget(MemoryWidgetType::Graph);
    });
    ui->mainToolBar->addAction(actGraph);

    QAction *actHex
        = new QAction(QIcon(QStringLiteral(":/img/icons/hexdump_light.svg")), tr("Hexdump"), this);
    actHex->setToolTip(tr("Show Hexdump view at current offset"));
    actHex->setObjectName(QStringLiteral("actionToolbarHexdump"));
    connect(actHex, &QAction::triggered, this, [this]() {
        showMemoryWidget(MemoryWidgetType::Hexdump);
    });
    ui->mainToolBar->addAction(actHex);

    QAction *actTypes
        = new QAction(QIcon(QStringLiteral(":/img/icons/list.svg")), tr("Types"), this);
    actTypes->setToolTip(tr("Toggle Types panel"));
    actTypes->setShortcut(QKeySequence(Qt::Key_T));
    actTypes->setObjectName(QStringLiteral("actionToolbarTypes"));
    connect(actTypes, &QAction::triggered, this, [this]() {
        if (typesDock) {
            bool v = typesDock->isVisible();
            typesDock->setVisible(!v);
            if (!v) {
                typesDock->raise();
            }
        }
    });
    ui->mainToolBar->addAction(actTypes);

    ui->mainToolBar->addSeparator();

    DebugActions *debugActions = new DebugActions(ui->mainToolBar, this);

    // Debug menu
    auto debugViewAction = ui->menuDebug->addAction(tr("View"));
    setAppMenuIcon(this, debugViewAction, AppMenuIcon::View, QColor(126, 87, 194));
    debugViewAction->setMenu(ui->menuAddDebugWidgets);
    ui->menuDebug->addSeparator();
    setAppMenuIcon(this, debugActions->rarunProfile, AppMenuIcon::Profile, QColor(191, 128, 32));
    setAppMenuIcon(
        this, debugActions->actionContinueUntilCall, AppMenuIcon::Debug, QColor(216, 88, 64));
    setAppMenuIcon(
        this, debugActions->actionContinueUntilSyscall, AppMenuIcon::Syscalls, QColor(216, 88, 64));
    setAppMenuIcon(
        this, debugActions->actionContinueUntilAddress, AppMenuIcon::Map, QColor(216, 88, 64));
    setAppMenuIcon(
        this, debugActions->actionContinueUntilSignal, AppMenuIcon::Debug, QColor(216, 88, 64));
    setAppMenuIcon(
        this, debugActions->actionContinueUntilProgram, AppMenuIcon::Run, QColor(216, 88, 64));
    setAppMenuIcon(
        this, debugActions->actionContinueUntilRet, AppMenuIcon::NavigateBack, QColor(216, 88, 64));
    setAppMenuIcon(
        this, debugActions->actionContinueUntilMain, AppMenuIcon::Function, QColor(216, 88, 64));
    ui->menuDebug->addAction(debugActions->rarunProfile);
    ui->menuDebug->addSeparator();
    ui->menuDebug->addAction(debugActions->actionStart);
    ui->menuDebug->addAction(debugActions->actionStartEmul);
    ui->menuDebug->addAction(debugActions->actionAttach);
    ui->menuDebug->addAction(debugActions->actionStartRemote);
    ui->menuDebug->addSeparator();
    ui->menuDebug->addAction(debugActions->actionStep);
    ui->menuDebug->addAction(debugActions->actionStepOver);
    ui->menuDebug->addAction(debugActions->actionStepOut);
    ui->menuDebug->addSeparator();
    ui->menuDebug->addAction(debugActions->actionContinue);
    ui->menuDebug->addAction(debugActions->actionContinueUntilCall);
    ui->menuDebug->addAction(debugActions->actionContinueUntilSyscall);
    ui->menuDebug->addAction(debugActions->actionContinueUntilAddress);
    ui->menuDebug->addAction(debugActions->actionContinueUntilSignal);
    ui->menuDebug->addAction(debugActions->actionContinueUntilProgram);
    ui->menuDebug->addAction(debugActions->actionContinueUntilRet);

    // Sepparator between undo/redo and goto lineEdit
    QWidget *spacer4 = new QWidget();
    spacer4->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    spacer4->setStyleSheet("background-color: rgba(0,0,0,0)");
    spacer4->setMinimumSize(10, 10);
    spacer4->setMaximumWidth(10);
    ui->mainToolBar->addWidget(spacer4);

    // Omnibar LineEdit
    this->omnibar = new Omnibar(this);
    ui->mainToolBar->addWidget(this->omnibar);

    // Webserver status bar button — visible only while the server is running
    webserverButton = new QToolButton();
    webserverButton->setIcon(QIcon(":/img/icons/cloud.svg"));
    webserverButton->setToolTip(tr("Open web server in browser"));
    webserverButton->setStyleSheet("background-color: rgba(0,0,0,0)");
    webserverButton->setAutoRaise(true);
    webserverButton->hide();
    connect(webserverButton, &QToolButton::clicked, this, []() {
        QString port = Core()->getConfig("http.port");
        QString root = Core()->getConfig("http.ui");
        Core()->cmdRaw(QStringLiteral("open http://localhost:%1/%2").arg(port, root));
    });
    statusBar()->addPermanentWidget(webserverButton);

    tasksProgressIndicator = new ProgressIndicator();
    tasksProgressIndicator->setStyleSheet("background-color: rgba(0,0,0,0)");
    tasksProgressIndicator->hide();
    ui->mainToolBar->addWidget(tasksProgressIndicator);

    // Visual navigation tool bar
    this->visualNavbar = new VisualNavbar(this);
    this->visualNavbar->setMovable(false);
    this->visualNavbar->setAllowedAreas(Qt::AllToolBarAreas);
    auto navLoc = configuration->getVisualNavbarLocation();
    if (navLoc == Configuration::VisualNavbarLocation::SuperTop) {
        insertToolBar(ui->mainToolBar, visualNavbar);
        insertToolBarBreak(ui->mainToolBar);
    } else if (navLoc == Configuration::VisualNavbarLocation::SuperBottom) {
        // Will be positioned below status bar via repositionSuperBottomNavbar()
    } else {
        addToolBarBreak(Qt::TopToolBarArea);
        addToolBar(Qt::TopToolBarArea, visualNavbar);
    }
    QObject::connect(configuration, &Configuration::colorsUpdated, this, [this]() {
        this->visualNavbar->updateGraphicsScene();
    });
    QObject::connect(
        configuration,
        &Configuration::visualNavbarLocationChanged,
        this,
        &MainWindow::updateVisualNavbarLocation);
    QObject::connect(
        configuration, &Configuration::interfaceThemeChanged, this, &MainWindow::chooseThemeIcons);

    // Expose toolbar visibility in the Windows menu so users can re-show a
    // toolbar they hid through its right-click context menu.
    QMenu *toolbarsMenu = new QMenu(tr("Toolbars"), this);
    setAppMenuIcon(this, toolbarsMenu, AppMenuIcon::View, QColor(126, 87, 194));
    QAction *mainToolBarToggle = ui->mainToolBar->toggleViewAction();
    mainToolBarToggle->setText(tr("Main toolbar"));
    toolbarsMenu->addAction(mainToolBarToggle);
    if (visualNavbar) {
        QAction *navbarToggle = visualNavbar->toggleViewAction();
        navbarToggle->setText(tr("Navigation bar"));
        toolbarsMenu->addAction(navbarToggle);
    }
    QAction *statusBarToggle = new QAction(tr("Status bar"), toolbarsMenu);
    statusBarToggle->setCheckable(true);
    statusBarToggle->setChecked(statusBar()->isVisible());
    connect(statusBarToggle, &QAction::toggled, statusBar(), &QWidget::setVisible);
    toolbarsMenu->addAction(statusBarToggle);
    ui->menuWindows->insertMenu(ui->actionRefresh_contents, toolbarsMenu);
    ui->menuWindows->insertSeparator(ui->actionRefresh_contents);
}

QMenu *MainWindow::createPopupMenu()
{
    QMenu *menu = new QMenu(this);
    menu->setTitle(tr("Toolbar"));

    QMenu *buttonsMenu = menu->addMenu(tr("Toolbar buttons"));
    for (QAction *action : ui->mainToolBar->actions()) {
        if (action->isSeparator()) {
            buttonsMenu->addSeparator();
            continue;
        }
        QString label = action->text();
        if (label.isEmpty()) {
            label = action->toolTip();
        }
        if (label.isEmpty()) {
            continue;
        }
        QAction *toggle = new QAction(label, buttonsMenu);
        toggle->setCheckable(true);
        toggle->setChecked(action->isVisible());
        if (!action->toolTip().isEmpty()) {
            toggle->setToolTip(action->toolTip());
        }
        connect(toggle, &QAction::toggled, action, &QAction::setVisible);
        buttonsMenu->addAction(toggle);
    }
    buttonsMenu->setToolTipsVisible(true);

    menu->addSeparator();
    QAction *mainToggle = ui->mainToolBar->toggleViewAction();
    mainToggle->setText(tr("Main toolbar"));
    menu->addAction(mainToggle);
    if (visualNavbar) {
        QAction *navToggle = visualNavbar->toggleViewAction();
        navToggle->setText(tr("Navigation bar"));
        menu->addAction(navToggle);
    }
    QAction *statusToggle = new QAction(tr("Status bar"), menu);
    statusToggle->setCheckable(true);
    statusToggle->setChecked(statusBar()->isVisible());
    connect(statusToggle, &QAction::toggled, statusBar(), &QWidget::setVisible);
    menu->addAction(statusToggle);

    return menu;
}

void MainWindow::initDocks()
{
    dockWidgets.reserve(20);
    consoleDock = new ConsoleWidget(this);

    overviewDock = new OverviewWidget(this);
    overviewDock->hide();
    actionOverview = overviewDock->toggleViewAction();
    connect(overviewDock, &OverviewWidget::isAvailableChanged, this, [this](bool isAvailable) {
        actionOverview->setEnabled(isAvailable);
    });
    actionOverview->setEnabled(overviewDock->getIsAvailable());
    actionOverview->setChecked(overviewDock->getUserOpened());

    dashboardDock = new Dashboard(this);
    functionsDock = new FunctionsWidget(this);
    typesDock = new TypesWidget(this);
    searchDock = new SearchWidget(this);
    commentsDock = new CommentsWidget(this);
    stringsDock = new StringsWidget(this);

    QList<IaitoDockWidget *> debugDocks
        = {stackDock = new StackWidget(this),
           threadsDock = new ThreadsWidget(this),
           processesDock = new ProcessesWidget(this),
           backtraceDock = new BacktraceWidget(this),
           registersDock = new RegistersWidget(this),
           memoryMapDock = new MemoryMapWidget(this),
           breakpointDock = new BreakpointWidget(this),
           registerRefsDock = new RegisterRefsWidget(this)};

    QList<IaitoDockWidget *> ioDocks
        = {filesDock = new FilesWidget(this),
           binariesDock = new BinariesWidget(this),
           mapsDock = new MapsWidget(this)};
    QList<IaitoDockWidget *> infoDocks = {
        classesDock = new ClassesWidget(this),
        entrypointDock = new EntrypointWidget(this),
        exportsDock = new ExportsWidget(this),
        flagsDock = new FlagsWidget(this),
        headersDock = new HeadersWidget(this),
        importsDock = new ImportsWidget(this),
        relocsDock = new RelocsWidget(this),
        resourcesDock = new ResourcesWidget(this),
        sdbDock = new SdbWidget(this),
        sectionsDock = new SectionsWidget(this),
        segmentsDock = new SegmentsWidget(this),
        stringsDock,
        symbolsDock = new SymbolsWidget(this),
        typesDock,
        commentsDock,
        nullptr,
        vTablesDock = new VTablesWidget(this),
        zignaturesDock = new ZignaturesWidget(this),
        nullptr,
        r2GraphDock = new R2GraphWidget(this),
        callGraphDock = new CallGraphWidget(this, false),
        globalCallGraphDock = new CallGraphWidget(this, true),
        zoomDock = new ZoomWidget(this),
    };
    QList<IaitoDockWidget *> codeDocks = {
        xrefsDock = new XrefsWidget(this),
        refsDock = new RefsWidget(this),
    };

    auto makeActionList = [this](QList<IaitoDockWidget *> docks) {
        QList<QAction *> result;
        for (auto dock : docks) {
            if (dock != nullptr) {
                result.push_back(dock->toggleViewAction());
            } else {
                auto separator = new QAction(this);
                separator->setSeparator(true);
                result.push_back(separator);
            }
        }
        return result;
    };

    searchDock->toggleViewAction()->setText(tr("Search..."));
    ui->menuCode->addSeparator();
    ui->menuCode->addActions(makeActionList(codeDocks));
    QList<IaitoDockWidget *> windowDocks2 = {
        consoleDock,
    };
    QList<IaitoDockWidget *> mainViewDocks = {
        dashboardDock,
        functionsDock,
        searchDock,
    };
    ui->menuView->insertSeparator(ui->menuZoom->menuAction());
    ui->menuView->insertActions(ui->menuZoom->menuAction(), makeActionList(windowDocks2));
    ui->menuView->insertActions(ui->menuZoom->menuAction(), makeActionList(mainViewDocks));
    ui->menuView->insertSeparator(ui->menuZoom->menuAction());
    ui->menuAddInfoWidgets->addActions(makeActionList(infoDocks));
    ui->menuAddIoWidgets->addActions(makeActionList(ioDocks));
    QAction *actionFilesystem = new QAction("Filesystem", this);
    connect(actionFilesystem, &QAction::triggered, this, [this]() {
        if (!filesystemDock) {
            filesystemDock = new FilesystemWidget(this);
            addExtraWidget(filesystemDock);
        }
        filesystemDock->show();
    });
    ui->menuAddIoWidgets->addAction(actionFilesystem);
    ui->menuAddDebugWidgets->addActions(makeActionList(debugDocks));

    auto uniqueDocks = mainViewDocks + windowDocks2 + infoDocks + codeDocks + ioDocks + debugDocks;
    uniqueDocks.append(overviewDock);
    for (auto dock : uniqueDocks) {
        if (dock) { // ignore nullptr used as separators
            addWidget(dock);
        }
    }
}

void MainWindow::applyTopLevelMenuIcons()
{
    const QColor fileColor(66, 133, 244);
    const QColor importColor(0, 150, 136);
    const QColor exportColor(245, 166, 35);
    const QColor runColor(67, 160, 71);
    const QColor editColor(216, 88, 64);
    const QColor viewColor(0, 150, 136);
    const QColor analysisColor(0, 137, 190);
    const QColor pluginColor(142, 36, 170);
    const QColor toolColor(191, 128, 32);
    const QColor windowColor(126, 87, 194);
    const QColor helpColor(56, 142, 60);

    // File: keep icons on the commands that change session/project state.
    setAppMenuIcon(this, ui->actionNew, AppMenuIcon::Open, fileColor);
    setAppMenuIcon(this, ui->actionMap, AppMenuIcon::Map, fileColor);
    setAppMenuIcon(this, ui->actionManageAddressSpace, AppMenuIcon::Manage, fileColor);
    setAppMenuIcon(this, ui->actionSave, AppMenuIcon::Save, fileColor);
    setAppMenuIcon(this, ui->actionSaveAs, AppMenuIcon::Save, fileColor);
    setAppMenuIcon(this, ui->menuImport, AppMenuIcon::Import, importColor);
    setAppMenuIcon(this, ui->menuExport, AppMenuIcon::Export, exportColor);
    setAppMenuIcon(this, ui->menuRun, AppMenuIcon::Run, runColor);
    setAppMenuIcon(this, ui->actionCommitChanges, AppMenuIcon::Commit, editColor);
    setAppMenuIcon(this, ui->actionSettings, AppMenuIcon::Settings, windowColor);
    setAppMenuIcon(this, ui->actionQuit, AppMenuIcon::Quit, editColor);

    setAppMenuIcon(this, ui->actionRun_Script, AppMenuIcon::Script, runColor);
    setAppMenuIcon(this, ui->actionRunR2jsScript, AppMenuIcon::Script, runColor);
    setAppMenuIcon(this, ui->actionScripts, AppMenuIcon::Script, runColor);

    // Edit: navigation already has toolbar icons, add write/edit semantics.
    setAppMenuIcon(this, ui->actionUndoWrite, AppMenuIcon::Undo, editColor);
    setAppMenuIcon(this, ui->actionRedoWrite, AppMenuIcon::Redo, editColor);
    setAppMenuIcon(this, ui->actionPatchInstruction, AppMenuIcon::Patch, editColor);
    setAppMenuIcon(this, ui->actionPatchString, AppMenuIcon::Patch, editColor);
    setAppMenuIcon(this, ui->actionPatchBytes, AppMenuIcon::Patch, editColor);
    setAppMenuIcon(this, ui->menuSetMode, AppMenuIcon::IOMode, editColor);

    // View: icon the feature submenus, not every dock toggle.
    setAppMenuIcon(this, ui->actionHighlight, AppMenuIcon::Highlight, viewColor);
    setAppMenuIcon(this, ui->menuAddInfoWidgets, AppMenuIcon::Info, viewColor);
    setAppMenuIcon(this, ui->menuAddIoWidgets, AppMenuIcon::Storage, viewColor);
    setAppMenuIcon(this, ui->menuAddAnother, AppMenuIcon::Extra, viewColor);
    setAppMenuIcon(this, ui->menuZoom, AppMenuIcon::ZoomIn, viewColor);
    setAppMenuIcon(this, ui->actionZoomIn, AppMenuIcon::ZoomIn, viewColor);
    setAppMenuIcon(this, ui->actionZoomOut, AppMenuIcon::ZoomOut, viewColor);
    setAppMenuIcon(this, ui->actionZoomReset, AppMenuIcon::ZoomReset, viewColor);
    if (consoleDock) {
        setAppMenuIcon(this, consoleDock->toggleViewAction(), AppMenuIcon::Console, viewColor);
    }
    if (dashboardDock) {
        setAppMenuIcon(this, dashboardDock->toggleViewAction(), AppMenuIcon::Dashboard, viewColor);
    }
    if (searchDock) {
        setAppMenuIcon(this, searchDock->toggleViewAction(), AppMenuIcon::Search, viewColor);
    }

    // Code: analysis and cross-reference commands are the high-signal actions.
    setAppMenuIcon(this, ui->actionAnalyze, AppMenuIcon::Analysis, analysisColor);
    setAppMenuIcon(this, ui->actionAnalyzeFunction, AppMenuIcon::Function, analysisColor);
    setAppMenuIcon(this, ui->actionAutonameAll, AppMenuIcon::Autoname, analysisColor);
    setAppMenuIcon(this, ui->actionAnalysisSettings, AppMenuIcon::Settings, analysisColor);
    setAppMenuIcon(this, ui->menuPlugins, AppMenuIcon::Plugin, pluginColor);
    setAppMenuIcon(this, ui->actionR2pm, AppMenuIcon::Package, pluginColor);
    if (xrefsDock) {
        setAppMenuIcon(this, xrefsDock->toggleViewAction(), AppMenuIcon::Xrefs, analysisColor);
    }
    if (refsDock) {
        setAppMenuIcon(this, refsDock->toggleViewAction(), AppMenuIcon::Refs, analysisColor);
    }

    // Tools: the hand-launched utilities benefit from fast visual scanning.
    setAppMenuIcon(this, ui->actionStart_Web_Server, AppMenuIcon::Web, toolColor);
    setAppMenuIcon(this, ui->actionPackageManagerPrefs, AppMenuIcon::Package, pluginColor);

    // Window: layout/state actions only; dense panel lists stay quiet.
    setAppMenuIcon(this, ui->actionRefresh_contents, AppMenuIcon::Refresh, windowColor);
    setAppMenuIcon(this, ui->menuWindowLayout, AppMenuIcon::Layout, windowColor);
    setAppMenuIcon(this, ui->actionDefault, AppMenuIcon::Layout, windowColor);
    setAppMenuIcon(this, ui->actionSaveLayout, AppMenuIcon::Save, windowColor);
    setAppMenuIcon(this, ui->actionManageLayouts, AppMenuIcon::Manage, windowColor);
    setAppMenuIcon(this, ui->menuLayouts, AppMenuIcon::Layout, windowColor);
    setAppMenuIcon(this, ui->actionUnlock, AppMenuIcon::Lock, windowColor);
    ui->actionUnlock->setIconVisibleInMenu(true);
    setAppMenuIcon(this, ui->actionReset_settings, AppMenuIcon::Reset, editColor);

    setAppMenuIcon(this, ui->actionExtraDecompiler, AppMenuIcon::Decompiler, viewColor);
    setAppMenuIcon(this, ui->actionExtraDisassembly, AppMenuIcon::Disassembly, viewColor);
    setAppMenuIcon(this, ui->actionExtraGraph, AppMenuIcon::Graph, viewColor);
    setAppMenuIcon(this, ui->actionExtraHexdump, AppMenuIcon::Hexdump, viewColor);
    setAppMenuIcon(this, ui->actionAddCustomCommand, AppMenuIcon::CustomCommand, viewColor);

    // Help: external destinations and app metadata.
    setAppMenuIcon(this, ui->actionIssue, AppMenuIcon::Issue, editColor);
    setAppMenuIcon(this, ui->actionWebsite, AppMenuIcon::Website, helpColor);
    setAppMenuIcon(this, ui->actionBook, AppMenuIcon::Book, helpColor);
    setAppMenuIcon(this, ui->actionFortune, AppMenuIcon::Fortune, exportColor);
    setAppMenuIcon(this, ui->actionCheckForUpdates, AppMenuIcon::Update, fileColor);
    setAppMenuIcon(this, ui->actionAbout, AppMenuIcon::About, helpColor);
}

void MainWindow::fillTopLevelMenuIconGaps()
{
    QSet<QMenu *> visited;
    for (QAction *action : ui->menuBar->actions()) {
        fillMenuIconGaps(this, action ? action->menu() : nullptr, visited);
    }
}

void MainWindow::toggleOverview(bool visibility, GraphWidget *targetGraph)
{
    if (!overviewDock) {
        return;
    }
    if (visibility) {
        overviewDock->setTargetGraphWidget(targetGraph);
    }
}

void MainWindow::updateTasksIndicator()
{
    bool running = core->getAsyncTaskManager()->getTasksRunning();
    tasksProgressIndicator->setVisible(running);
    tasksProgressIndicator->setProgressIndicatorVisible(running);
}

void MainWindow::addExtraGraph()
{
    auto *extraDock = new GraphWidget(this);
    addExtraWidget(extraDock);
}

void MainWindow::addExtraHexdump()
{
    auto *extraDock = new HexdumpWidget(this);
    addExtraWidget(extraDock);
}

void MainWindow::addExtraDisassembly()
{
    auto *extraDock = new DisassemblyWidget(this);
    addExtraWidget(extraDock);
}

void MainWindow::addExtraDecompiler()
{
    auto *extraDock = new DecompilerWidget(this);
    addExtraWidget(extraDock);
}

void MainWindow::addExtraCustomCommand()
{
    auto *extraDock = new CustomCommandWidget(this);
    addExtraWidget(extraDock);
}

void MainWindow::rebuildAnalyzePluginsMenu()
{
    auto insertAction = [this](QAction *before, QAction *action) {
        if (before) {
            ui->menuPlugins->insertAction(before, action);
        } else {
            ui->menuPlugins->addAction(action);
        }
    };

    for (QAction *action : ui->menuPlugins->actions()) {
        if (action->property(analyzePluginDynamicActionProperty).toBool()) {
            delete action;
        }
    }

    QMap<QString, QString> pluginDescriptions;
    for (const auto &plugin : Core()->getRAnalPluginDescriptions()) {
        pluginDescriptions.insert(plugin.name, plugin.description);
    }

    QMap<QString, AnalyzePluginCommandEntry> entriesByName;
    for (const auto &pluginName : Core()->getAnalPluginNames()) {
        const QString pluginDescription = pluginDescriptions.value(pluginName);
        const QString help = Core()->cmdRaw(QStringLiteral("a:%1?").arg(pluginName));

        bool parsedAnyLine = false;
        const auto lines = help.split(QLatin1Char('\n'));
        for (const auto &line : lines) {
            AnalyzePluginCommandEntry parsedEntry;
            if (!parseAnalyzePluginLine(line, parsedEntry)) {
                continue;
            }
            if (parsedEntry.description.isEmpty()) {
                parsedEntry.description = pluginDescription;
            }
            mergeAnalyzePluginCommandEntry(entriesByName, std::move(parsedEntry));
            parsedAnyLine = true;
        }

        if (!parsedAnyLine) {
            mergeAnalyzePluginCommandEntry(
                entriesByName,
                {QStringLiteral("a:%1").arg(pluginName),
                 QString(),
                 pluginDescription.isEmpty() ? tr("analysis plugin command") : pluginDescription});
        }
    }

    QAction *insertBefore = ui->actionR2pm;
    if (!entriesByName.isEmpty() && insertBefore) {
        insertBefore = ui->menuPlugins->insertSeparator(insertBefore);
        insertBefore->setProperty(analyzePluginDynamicActionProperty, true);
    }

    for (auto it = entriesByName.cbegin(); it != entriesByName.cend(); ++it) {
        const auto &entry = it.value();
        const QString actionText = entry.args.isEmpty()
                                       ? entry.name
                                       : QStringLiteral("%1 %2").arg(entry.name, entry.args);
        auto *action = new QAction(actionText, ui->menuPlugins);
        setAppMenuIcon(this, action, AppMenuIcon::Analysis, QColor(0, 137, 190));
        action->setToolTip(entry.description);
        action->setStatusTip(
            buildAnalyzePluginStatusText(entry.name, entry.args, entry.description));
        action->setProperty(analyzePluginDynamicActionProperty, true);
        connect(action, &QAction::triggered, this, [this, name = entry.name, args = entry.args]() {
            executeAnalyzePluginCommand(name, args);
        });
        insertAction(insertBefore, action);
    }

    if (entriesByName.isEmpty()) {
        auto *emptyAction = new QAction(tr("No analysis plugin commands"), ui->menuPlugins);
        emptyAction->setProperty(analyzePluginDynamicActionProperty, true);
        emptyAction->setEnabled(false);
        insertAction(insertBefore, emptyAction);
    }
    fillTopLevelMenuIconGaps();
}

QString MainWindow::promptForAnalyzePluginCommand(const QString &name, const QString &args)
{
    QStringList commandParts;
    commandParts.append(name);

    for (const auto &placeholder : extractAnalyzePluginPlaceholders(args)) {
        bool ok = false;
        const QString value = QInputDialog::getText(
                                  this,
                                  tr("Analysis Plugin"),
                                  tr("%1 %2:").arg(name, placeholder.label),
                                  QLineEdit::Normal,
                                  QString(),
                                  &ok)
                                  .trimmed();
        if (!ok) {
            return QString();
        }
        if (value.isEmpty() && !placeholder.optional) {
            return QString();
        }
        if (value.isEmpty()) {
            continue;
        }
        commandParts.append(value);
    }

    return commandParts.join(QStringLiteral(" "));
}

void MainWindow::executeAnalyzePluginCommand(const QString &name, const QString &args)
{
    if (name.isEmpty()) {
        return;
    }

    const QString command = args.isEmpty() ? name : promptForAnalyzePluginCommand(name, args);
    if (command.isEmpty()) {
        return;
    }

    auto *outputDock = new CustomCommandWidget(this);
    outputDock->setWindowTitle(tr("Analysis Plugin: %1").arg(command));
    addExtraWidget(outputDock);
    outputDock->executeCommand(command);
    refreshAll();
}

void MainWindow::addExtraWidget(IaitoDockWidget *extraDock)
{
    extraDock->setTransient(true);
    dockOnMainArea(extraDock);
    addWidget(extraDock);
    extraDock->show();
    extraDock->raise();
}

QMenu *MainWindow::getMenuByType(MenuType type)
{
    switch (type) {
    case MenuType::File:
        return ui->menuFile;
    case MenuType::Edit:
        return ui->menuEdit;
    case MenuType::Code:
        return ui->menuCode;
    case MenuType::View:
        return ui->menuView;
    case MenuType::Tools:
        return ui->menuTools;
    case MenuType::Windows:
        return ui->menuWindows;
    case MenuType::Debug:
        return ui->menuDebug;
    case MenuType::Help:
        return ui->menuHelp;
    case MenuType::Plugins:
        return ui->menuPlugins;
    default:
        return nullptr;
    }
}

void MainWindow::addPluginDockWidget(IaitoDockWidget *dockWidget)
{
    addWidget(dockWidget);
    ui->menuPlugins->addAction(dockWidget->toggleViewAction());
    addDockWidget(Qt::DockWidgetArea::TopDockWidgetArea, dockWidget);
    pluginDocks.push_back(dockWidget);
}

void MainWindow::addMenuFileAction(QAction *action)
{
    ui->menuFile->addAction(action);
}

void MainWindow::openCurrentCore(InitialOptions &options, bool skipOptionsDialog)
{
    Q_UNUSED(skipOptionsDialog);
    RCore *core = iaitoPluginCore();
    if (core == nullptr) {
        return;
    }
    // filename taken from r_core
    options.filename = r_core_cmd_strf(core, "i~^file[1]");
    setFilename(options.filename);

    finalizeOpen();
    /* Show analysis options dialog */
    // displayInitialOptionsDialog(options, skipOptionsDialog);
}

void MainWindow::openNewFile(InitialOptions &options, bool skipOptionsDialog)
{
    setFilename(options.filename);

    /* Prompt to load filename.r2 script */
    if (options.script.isEmpty()) {
        QString script = QStringLiteral("%1.r2").arg(this->filename);
        if (r_file_exists(script.toStdString().data())) {
            QMessageBox mb;
            mb.setWindowTitle(tr("Script loading"));
            mb.setText(tr("Do you want to load the '%1' script?").arg(script));
            mb.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            if (mb.exec() == QMessageBox::Yes) {
                options.script = script;
            }
        }
    }

    /* Show analysis options dialog */
    displayInitialOptionsDialog(options, skipOptionsDialog);
}

void MainWindow::openNewFileFailed()
{
    displayNewFileDialog();
    QMessageBox mb(this);
    mb.setIcon(QMessageBox::Critical);
    mb.setStandardButtons(QMessageBox::Ok);
    mb.setWindowTitle(tr("Cannot open file!"));
    mb.setText(
        tr("Could not open the file! Make sure the file exists and that "
           "you have the correct permissions."));
    mb.exec();
}

/**
 * @brief displays the WelocmeDialog
 *
 * Upon first execution of Iaito, the WelcomeDialog would be showed to the user.
 * The Welcome dialog would be showed after a reset of Iaito's settings by
 * the user.
 */

void MainWindow::displayWelcomeDialog()
{
    WelcomeDialog w;
    w.exec();
}

void MainWindow::displayNewFileDialog()
{
    NewFileDialog *n = new NewFileDialog(this);
    newFileDialog = n;
    n->setAttribute(Qt::WA_DeleteOnClose);
    n->show();
}

void MainWindow::closeNewFileDialog()
{
    if (newFileDialog) {
        newFileDialog->close();
    }
    newFileDialog = nullptr;
}

void MainWindow::displayInitialOptionsDialog(
    const InitialOptions &options, bool skipOptionsDialog, bool reuseCurrentFile)
{
    auto o = new InitialOptionsDialog(this);
    o->setAttribute(Qt::WA_DeleteOnClose);
    o->setReuseCurrentFile(reuseCurrentFile);
    o->loadOptions(options);

    if (skipOptionsDialog) {
        o->setupAndStartAnalysis();
    } else {
        o->show();
    }
}

void MainWindow::openProject(const QString &project_name)
{
    if (!IaitoCore::isProjectNameValid(project_name)) {
        QMessageBox::critical(this, tr("Error"), tr("Invalid project name."));
        return;
    }

    QString filename = core->cmdRaw("Pi " + project_name);
    setFilename(filename.trimmed());

    core->openProject(project_name);
    updateSaveProjectAction();

    finalizeOpen();
}

void MainWindow::finalizeOpen()
{
    core->getOpcodes();
    core->updateSeek();
    refreshAll();
    // Add fortune message
    core->message("\n" + core->cmdRaw("fo"));

    // hide all docks before showing window to avoid false positive for
    // refreshDeferrer
    for (auto dockWidget : dockWidgets) {
        dockWidget->hide();
    }

    QSettings settings;
    auto geometry = settings.value("geometry").toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
        show();
    } else {
        showMaximized();
    }

    Config()->adjustColorThemeDarkness();
    setViewLayout(getViewLayout(core->currentlyDebugging ? LAYOUT_DEBUG : LAYOUT_DEFAULT));

    // Set focus to disasm or graph widget
    // Graph with function in it has focus priority over DisasmWidget.
    // If there are no graph/disasm widgets focus on MainWindow

    setFocus();
    bool graphContainsFunc = false;
    for (auto dockWidget : dockWidgets) {
        auto graphWidget = qobject_cast<GraphWidget *>(dockWidget);
        if (graphWidget && dockWidget->isVisibleToUser()) {
            graphContainsFunc = !graphWidget->getGraphView()->getBlocks().empty();
            if (graphContainsFunc) {
                dockWidget->raiseMemoryWidget();
                break;
            }
        }
        auto disasmWidget = qobject_cast<DisassemblyWidget *>(dockWidget);
        if (disasmWidget && dockWidget->isVisibleToUser()) {
            disasmWidget->raiseMemoryWidget();
            // continue looping in case there is a graph widget
        }
        auto decompilerWidget = qobject_cast<DecompilerWidget *>(dockWidget);
        if (decompilerWidget && dockWidget->isVisibleToUser()) {
            decompilerWidget->raiseMemoryWidget();
            // continue looping in case there is a graph widget
        }
    }
    consoleDock->hide();
    // Wire up status bar updates for all seekable views
    // Disassembly widgets
    for (auto disasm : findChildren<DisassemblyWidget *>()) {
        if (auto sk = disasm->getSeekable()) {
            connect(sk, &IaitoSeekable::seekableSeekChanged, this, [this](RVA addr) {
                updateStatusBar(addr);
            });
        }
    }
    // Graph widgets
    for (auto graphDock : findChildren<GraphWidget *>()) {
        // Seekable for graph
        if (auto sk = graphDock->getSeekable()) {
            connect(sk, &IaitoSeekable::seekableSeekChanged, this, [this](RVA addr) {
                updateStatusBar(addr);
            });
        }
        // Graph name changes (e.g., function)
        if (auto dg = qobject_cast<DisassemblerGraphView *>(graphDock->getGraphView())) {
            connect(dg, &DisassemblerGraphView::nameChanged, this, [this, dg](const QString &name) {
                updateStatusBar(dg->currentFcnAddr, name);
            });
        }
    }

    // Signal that the UI is ready for background tasks to run safely.
    // Use a single-shot queued invocation so that uiReady is emitted after the
    // current initialization events are processed (avoids re-entrancy into
    // painting/layout while the window is still finalizing).
    QTimer::singleShot(0, this, [this]() {
        uiReadyFlag = true;
        emit uiReady();
    });
}

bool MainWindow::saveProject(bool quit)
{
    QString projectName = core->getConfig("prj.name");
    if (projectName.isEmpty()) {
        return saveProjectAs(quit);
    }
    if (core->getConfigb("cfg.debug")) {
        QMessageBox::warning(this, tr("Error"), tr("You can't save a project while debugging"));
        return false;
    }
    core->saveProject(projectName);
    return true;
}

bool MainWindow::saveProjectAs(bool quit)
{
    if (core->getConfigb("cfg.debug")) {
        QMessageBox::warning(this, tr("Error"), tr("You can't save a project while debugging"));
        return false;
    }
    SaveProjectDialog dialog(quit, this);
    return SaveProjectDialog::Rejected != dialog.exec();
}

void MainWindow::refreshOmniBar(const QStringList &flags)
{
    omnibar->refresh(flags);
}

void MainWindow::setFilename(const QString &fn)
{
    // Add file name to window title
    this->filename = fn;
    this->setWindowTitle(APPNAME " – " + fn);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    RCore *kore = iaitoPluginCore();
    if (kore != nullptr) {
        // TODO: restore configs :)
        consoleDock->unredirectOutput();
        // qApp->quit();
        r_config_set_i(kore->config, "scr.color", 2);
        r_config_set_i(kore->config, "scr.html", 0);
        QMainWindow::closeEvent(event);
        return;
    }
    if (this->filename == "") {
        QMainWindow::closeEvent(event);
        return;
    }

    // Check if there are uncommitted changes
    if (!ioModesController.askCommitUnsavedChanges()) {
        // if false, Cancel was chosen
        event->ignore();
        return;
    }

    QMessageBox::StandardButton ret = QMessageBox::question(
        this,
        APPNAME,
        tr("Do you really want to exit?\nSave your project before closing!"),
        (QMessageBox::StandardButtons) (QMessageBox::Save | QMessageBox::Discard
                                        | QMessageBox::Cancel));
    if (ret == QMessageBox::Cancel) {
        event->ignore();
        return;
    }
    if (ret == QMessageBox::Discard) {
        event->ignore();
        QMainWindow::closeEvent(event);
        return;
    }

    if (ret == QMessageBox::Save && !saveProject(true)) {
        event->ignore();
        return;
    }
    // discard (do not save)
    if (core->currentlyDebugging) {
        core->stopDebug();
    } else {
        saveSettings();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    /*
     * Dirty hack
     * Just to adjust the width of Functions Widget to fixed size.
     * After everything is drawn, restore the max width limit.
     */
    if (functionsDock && functionDockWidthToRestore) {
        functionsDock->setMaximumWidth(functionDockWidthToRestore);
        functionDockWidthToRestore = 0;
    }
}

void MainWindow::readSettings()
{
    QSettings settings;

    responsive = settings.value("responsive").toBool();
    lockDocks(settings.value("panelLock").toBool());
    tabsOnTop = settings.value("tabsOnTop", true).toBool();
    setTabLocation();
    bool dockGroupedDragging = settings.value("docksGroupedDragging", false).toBool();
    ui->actionGrouped_dock_dragging->setChecked(dockGroupedDragging);
    on_actionGrouped_dock_dragging_triggered(dockGroupedDragging);

    loadLayouts(settings);
}

void MainWindow::saveSettings()
{
    QSettings settings;

    settings.setValue("panelLock", !ui->actionUnlock->isChecked());
    settings.setValue("tabsOnTop", tabsOnTop);
    settings.setValue("docksGroupedDragging", ui->actionGrouped_dock_dragging->isChecked());
    settings.setValue("geometry", saveGeometry());

    layouts[Core()->currentlyDebugging ? LAYOUT_DEBUG : LAYOUT_DEFAULT] = getViewLayout();
    saveLayouts(settings);
}

void MainWindow::setTabLocation()
{
    if (tabsOnTop) {
        this->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
        ui->actionTabs_on_Top->setChecked(true);
    } else {
        this->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::South);
        ui->actionTabs_on_Top->setChecked(false);
    }
}

void MainWindow::refreshAll()
{
    core->triggerRefreshAll();
}

void MainWindow::lockDocks(bool lock)
{
    if (ui->actionUnlock->isChecked() == lock) {
        ui->actionUnlock->setChecked(!lock);
    }
    if (lock) {
        for (QDockWidget *dockWidget : findChildren<QDockWidget *>()) {
            dockWidget->setFeatures(QDockWidget::NoDockWidgetFeatures);
            if (!dockWidget->titleBarWidget()) {
                dockWidget->setTitleBarWidget(new QWidget(dockWidget));
            }
        }
    } else {
        for (QDockWidget *dockWidget : findChildren<QDockWidget *>()) {
            dockWidget->setFeatures(
                QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
                | QDockWidget::DockWidgetFloatable);
            if (QWidget *tb = dockWidget->titleBarWidget()) {
                dockWidget->setTitleBarWidget(nullptr);
                tb->deleteLater();
            }
        }
    }
}

void MainWindow::restoreDocks()
{
    // Initial structure
    // func | main area | refs/xrefs
    //      |___________| overview
    //      | console   |
    addDockWidget(Qt::LeftDockWidgetArea, functionsDock);
    splitDockWidget(functionsDock, dashboardDock, Qt::Horizontal);
    splitDockWidget(dashboardDock, stackDock, Qt::Horizontal);
    splitDockWidget(dashboardDock, consoleDock, Qt::Vertical);

    // right side: refs/xrefs tabs above the graph overview
    splitDockWidget(stackDock, overviewDock, Qt::Vertical);
    tabifyDockWidget(stackDock, refsDock);
    tabifyDockWidget(refsDock, xrefsDock);

    // tabs next to functions on the left side
    tabifyDockWidget(functionsDock, symbolsDock);
    tabifyDockWidget(functionsDock, importsDock);
    tabifyDockWidget(functionsDock, exportsDock);

    // main area
    tabifyDockWidget(dashboardDock, entrypointDock);
    tabifyDockWidget(dashboardDock, flagsDock);
    tabifyDockWidget(dashboardDock, stringsDock);
    tabifyDockWidget(dashboardDock, relocsDock);
    tabifyDockWidget(dashboardDock, typesDock);
    tabifyDockWidget(dashboardDock, searchDock);
    tabifyDockWidget(dashboardDock, headersDock);
    tabifyDockWidget(dashboardDock, zignaturesDock);
    tabifyDockWidget(dashboardDock, classesDock);
    tabifyDockWidget(dashboardDock, resourcesDock);
    tabifyDockWidget(dashboardDock, vTablesDock);
    tabifyDockWidget(dashboardDock, sdbDock);
    tabifyDockWidget(dashboardDock, memoryMapDock);
    tabifyDockWidget(dashboardDock, breakpointDock);
    tabifyDockWidget(dashboardDock, registerRefsDock);
    tabifyDockWidget(dashboardDock, r2GraphDock);
    tabifyDockWidget(dashboardDock, callGraphDock);
    tabifyDockWidget(dashboardDock, globalCallGraphDock);
    tabifyDockWidget(dashboardDock, mapsDock);
    tabifyDockWidget(dashboardDock, filesDock);
    tabifyDockWidget(dashboardDock, binariesDock);
    mapsDock->hide();
    filesDock->hide();
    binariesDock->hide();
    tabifyDockWidget(dashboardDock, zoomDock);
    for (const auto &it : dockWidgets) {
        // Check whether or not current widgets is graph, hexdump or disasm
        if (isExtraMemoryWidget(it)) {
            tabifyDockWidget(dashboardDock, it);
        }
    }

    // Console | Sections/segments/comments
    splitDockWidget(consoleDock, sectionsDock, Qt::Horizontal);
    tabifyDockWidget(sectionsDock, segmentsDock);
    tabifyDockWidget(sectionsDock, commentsDock);

    // Add Stack, Registers, Threads and Backtrace vertically stacked
    splitDockWidget(stackDock, registersDock, Qt::Vertical);
    tabifyDockWidget(stackDock, backtraceDock);
    tabifyDockWidget(backtraceDock, threadsDock);
    tabifyDockWidget(threadsDock, processesDock);

    for (auto dock : pluginDocks) {
        dockOnMainArea(dock);
    }
    lockDocks(false);
}

bool MainWindow::isDebugWidget(QDockWidget *dock) const
{
    return dock == stackDock || dock == registersDock || dock == backtraceDock
           || dock == threadsDock || dock == memoryMapDock || dock == breakpointDock
           || dock == processesDock || dock == registerRefsDock;
}

bool MainWindow::isExtraMemoryWidget(QDockWidget *dock) const
{
    return qobject_cast<GraphWidget *>(dock) || qobject_cast<HexdumpWidget *>(dock)
           || qobject_cast<DisassemblyWidget *>(dock) || qobject_cast<DecompilerWidget *>(dock);
}

void MainWindow::applyDefaultSideDockWidths(QDockWidget *mainDock)
{
    clearDefaultSideDockWidths();

    const QList<QWidget *> sideDocks = {
        functionsDock,
        symbolsDock,
        importsDock,
        exportsDock,
        stackDock,
        refsDock,
        xrefsDock,
        overviewDock,
    };
    for (QWidget *dock : sideDocks) {
        if (dock) {
            defaultSideDockWidthConstraints.append(
                qMakePair(dock, qhelpers::forceWidth(dock, defaultSideDockWidth)));
        }
    }

    if (mainDock) {
        resizeDocks(
            {functionsDock, mainDock, refsDock},
            {defaultSideDockWidth,
             qMax(defaultSideDockWidth, width() - 2 * defaultSideDockWidth),
             defaultSideDockWidth},
            Qt::Horizontal);
    }
}

void MainWindow::clearDefaultSideDockWidths()
{
    for (auto &constraint : defaultSideDockWidthConstraints) {
        if (constraint.first) {
            constraint.second.restoreWidth(constraint.first);
        }
    }
    defaultSideDockWidthConstraints.clear();
}

MemoryWidgetType MainWindow::getMemoryWidgetTypeToRestore()
{
    if (lastSyncMemoryWidget) {
        return lastSyncMemoryWidget->getType();
    }
    return MemoryWidgetType::Disassembly;
}

QString MainWindow::getUniqueObjectName(const QString &widgetType) const
{
    QStringList docks;
    docks.reserve(dockWidgets.size());
    QString name;
    for (const auto &it : dockWidgets) {
        name = it->objectName();
        if (name.split(';').at(0) == widgetType) {
            docks.push_back(name);
        }
    }

    if (docks.isEmpty()) {
        return widgetType;
    }

    int id = 0;
    while (docks.contains(widgetType + ";" + QString::number(id))) {
        id++;
    }

    return widgetType + ";" + QString::number(id);
}

void MainWindow::showMemoryWidget()
{
    if (lastSyncMemoryWidget) {
        if (lastSyncMemoryWidget->tryRaiseMemoryWidget()) {
            return;
        }
    }
    showMemoryWidget(MemoryWidgetType::Disassembly);
}

void MainWindow::showMemoryWidget(MemoryWidgetType type)
{
    for (auto &dock : dockWidgets) {
        if (auto memoryWidget = qobject_cast<MemoryDockWidget *>(dock)) {
            if (memoryWidget->getType() == type && memoryWidget->getSeekable()->isSynchronized()) {
                memoryWidget->tryRaiseMemoryWidget();
                return;
            }
        }
    }
    auto memoryDockWidget = addNewMemoryWidget(type, Core()->getOffset());
    memoryDockWidget->raiseMemoryWidget();
}

QMenu *MainWindow::createShowInMenu(QWidget *parent, RVA address, AddressTypeHint addressType)
{
    QMenu *menu = new QMenu(parent);
    // Memory dock widgets
    for (auto &dock : dockWidgets) {
        if (auto memoryWidget = qobject_cast<MemoryDockWidget *>(dock)) {
            if (memoryWidget->getType() == MemoryWidgetType::Graph
                || memoryWidget->getType() == MemoryWidgetType::Decompiler) {
                if (addressType == AddressTypeHint::Data) {
                    continue;
                }
            }
            QAction *action = new QAction(memoryWidget->windowTitle(), menu);
            connect(action, &QAction::triggered, this, [memoryWidget, address]() {
                memoryWidget->getSeekable()->seek(address);
                memoryWidget->raiseMemoryWidget();
            });
            menu->addAction(action);
        }
    }
    menu->addSeparator();
    // Rest of the AddressableDockWidgets that weren't added already
    for (auto &dock : dockWidgets) {
        if (auto memoryWidget = qobject_cast<AddressableDockWidget *>(dock)) {
            if (qobject_cast<MemoryDockWidget *>(dock)) {
                continue;
            }
            QAction *action = new QAction(memoryWidget->windowTitle(), menu);
            connect(action, &QAction::triggered, this, [memoryWidget, address]() {
                memoryWidget->getSeekable()->seek(address);
                memoryWidget->raiseMemoryWidget();
            });
            menu->addAction(action);
        }
    }
    menu->addSeparator();
    auto createAddNewWidgetAction = [this, menu, address](QString label, MemoryWidgetType type) {
        QAction *action = new QAction(label, menu);
        connect(action, &QAction::triggered, this, [this, address, type]() {
            addNewMemoryWidget(type, address, false);
        });
        menu->addAction(action);
    };
    createAddNewWidgetAction(tr("New disassembly"), MemoryWidgetType::Disassembly);
    if (addressType != AddressTypeHint::Data) {
        createAddNewWidgetAction(tr("New graph"), MemoryWidgetType::Graph);
    }
    createAddNewWidgetAction(tr("New hexdump"), MemoryWidgetType::Hexdump);
    createAddNewWidgetAction(tr("New Decompiler"), MemoryWidgetType::Decompiler);
    return menu;
}

void MainWindow::setCurrentMemoryWidget(MemoryDockWidget *memoryWidget)
{
    if (memoryWidget->getSeekable()->isSynchronized()) {
        lastSyncMemoryWidget = memoryWidget;
    }
    lastMemoryWidget = memoryWidget;
}

MemoryDockWidget *MainWindow::getLastMemoryWidget()
{
    return lastMemoryWidget;
}

MemoryDockWidget *MainWindow::addNewMemoryWidget(
    MemoryWidgetType type, RVA address, bool synchronized)
{
    MemoryDockWidget *memoryWidget = nullptr;
    switch (type) {
    case MemoryWidgetType::Graph:
        memoryWidget = new GraphWidget(this);
        break;
    case MemoryWidgetType::Hexdump:
        memoryWidget = new HexdumpWidget(this);
        break;
    case MemoryWidgetType::Disassembly:
        memoryWidget = new DisassemblyWidget(this);
        break;
    case MemoryWidgetType::Decompiler:
        memoryWidget = new DecompilerWidget(this);
        break;
    }
    auto seekable = memoryWidget->getSeekable();
    seekable->setSynchronization(synchronized);
    seekable->seek(address);
    addExtraWidget(memoryWidget);
    return memoryWidget;
}

void MainWindow::initBackForwardMenu()
{
    auto prepareButtonMenu = [this](QAction *action) -> QMenu * {
        QToolButton *button = qobject_cast<QToolButton *>(ui->mainToolBar->widgetForAction(action));
        if (!button) {
            return nullptr;
        }
        QMenu *menu = new QMenu(button);
        button->setMenu(menu);
        button->setPopupMode(QToolButton::DelayedPopup);
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(
            button, &QWidget::customContextMenuRequested, button, [menu, button](const QPoint &pos) {
                menu->exec(button->mapToGlobal(pos));
            });

        QFontMetrics metrics(fontMetrics());
        // Roughly 10-16 lines depending on padding size, no need to calculate
        // more precisely
        menu->setMaximumHeight(metrics.lineSpacing() * 20);

        menu->setToolTipsVisible(true);
        return menu;
    };

    if (auto menu = prepareButtonMenu(ui->actionBackward)) {
        menu->setObjectName("historyMenu");
        connect(menu, &QMenu::aboutToShow, menu, [this, menu]() { updateHistoryMenu(menu, false); });
    }
    if (auto menu = prepareButtonMenu(ui->actionForward)) {
        menu->setObjectName("forwardHistoryMenu");
        connect(menu, &QMenu::aboutToShow, menu, [this, menu]() { updateHistoryMenu(menu, true); });
    }
}

void MainWindow::updateHistoryMenu(QMenu *menu, bool redo)
{
    // Not too long so that whole screen doesn't get covered,
    // not too short so that reasonable length c++ names can be seen most of the
    // time
    const int MAX_NAME_LENGTH = 64;

    auto hist = Core()->cmdj("sj");
    bool history = true;
    QList<QAction *> actions;
    for (auto item : Core()->cmdj("sj").array()) {
        QJsonObject obj = item.toObject();
        QString name = obj["name"].toString();
        RVA offset = obj["offset"].toVariant().toULongLong();
        bool current = obj["current"].toBool(false);
        if (current) {
            history = false;
        }
        if (history != redo || current) { // Include current in both directions
            QString addressString = RAddressString(offset);

            QString toolTip = QStringLiteral("%1 %2")
                                  .arg(addressString, name); // show non truncated name in tooltip

            name.truncate(MAX_NAME_LENGTH); // TODO:#1904 use common name
                                            // shortening function
            QString label = QStringLiteral("%1 (%2)").arg(name, addressString);
            if (current) {
                label = QStringLiteral("current position (%1)").arg(addressString);
            }
            QAction *action = new QAction(label, menu);
            action->setToolTip(toolTip);
            actions.push_back(action);
            if (current) {
                QFont font;
                font.setBold(true);
                action->setFont(font);
            }
        }
    }
    if (!redo) {
        std::reverse(actions.begin(), actions.end());
    }
    menu->clear();
    menu->addActions(actions);
    int steps = 1;
    for (QAction *item : menu->actions()) {
        if (redo) {
            connect(item, &QAction::triggered, item, [steps]() {
                for (int i = 0; i < steps; i++) {
                    Core()->seekNext();
                }
            });
        } else {
            connect(item, &QAction::triggered, item, [steps]() {
                for (int i = 0; i < steps; i++) {
                    Core()->seekPrev();
                }
            });
        }
        ++steps;
    }
}

void MainWindow::updateLayoutsMenu()
{
    ui->menuLayouts->clear();
    for (auto it = layouts.begin(), end = layouts.end(); it != end; ++it) {
        QString name = it.key();
        if (isBuiltinLayoutName(name)) {
            continue;
        }
        auto action = new QAction(it.key(), ui->menuLayouts);
        connect(action, &QAction::triggered, this, [this, name]() {
            setViewLayout(getViewLayout(name));
        });
        ui->menuLayouts->addAction(action);
    }
    fillTopLevelMenuIconGaps();
}

void MainWindow::saveNamedLayout()
{
    bool ok = false;
    QString name;
    QStringList names = layouts.keys();
    names.removeAll(LAYOUT_DEBUG);
    names.removeAll(LAYOUT_DEFAULT);
    while (name.isEmpty() || isBuiltinLayoutName(name)) {
        if (ok) {
            QMessageBox::warning(
                this, tr("Save layout error"), tr("'%1' is not a valid name.").arg(name));
        }
        name = QInputDialog::getItem(this, tr("Save layout"), tr("Enter name"), names, -1, true, &ok);
        if (!ok) {
            return;
        }
    }
    layouts[name] = getViewLayout();
    updateLayoutsMenu();
    saveSettings();
}

void MainWindow::manageLayouts()
{
    LayoutManager layoutManger(layouts, this);
    layoutManger.exec();
    updateLayoutsMenu();
}

void MainWindow::addWidget(IaitoDockWidget *widget)
{
    dockWidgets.push_back(widget);
}

void MainWindow::addMemoryDockWidget(MemoryDockWidget *widget)
{
    connect(widget, &QDockWidget::visibilityChanged, this, [this, widget](bool visibility) {
        if (visibility) {
            setCurrentMemoryWidget(widget);
        }
    });
}

void MainWindow::removeWidget(IaitoDockWidget *widget)
{
    dockWidgets.removeAll(widget);
    pluginDocks.removeAll(widget);
    if (lastSyncMemoryWidget == widget) {
        lastSyncMemoryWidget = nullptr;
    }
    if (lastMemoryWidget == widget) {
        lastMemoryWidget = nullptr;
    }
}

void MainWindow::showZenDocks()
{
    const QList<QDockWidget *> zenDocks
        = {functionsDock, symbolsDock, importsDock, exportsDock, refsDock, xrefsDock, overviewDock};
    QDockWidget *widgetToFocus = nullptr;
    for (auto w : dockWidgets) {
        if (zenDocks.contains(w) || isExtraMemoryWidget(w)) {
            w->show();
        }
        if (qobject_cast<DisassemblyWidget *>(w)) {
            widgetToFocus = w;
        }
    }
    functionsDock->raise();
    refsDock->raise();
    applyDefaultSideDockWidths(widgetToFocus);
    if (widgetToFocus) {
        widgetToFocus->raise();
    }
}

void MainWindow::showDebugDocks()
{
    const QList<QDockWidget *> debugDocks
        = {functionsDock,
           stringsDock,
           searchDock,
           stackDock,
           registersDock,
           backtraceDock,
           threadsDock,
           memoryMapDock,
           breakpointDock};
    functionDockWidthToRestore = functionsDock->maximumWidth();
    functionsDock->setMaximumWidth(200);
    auto registerWidth = qhelpers::forceWidth(registersDock, std::min(500, this->width() / 4));
    auto registerHeight = qhelpers::forceHeight(registersDock, std::max(100, height() / 2));
    QDockWidget *widgetToFocus = nullptr;
    for (auto w : dockWidgets) {
        if (debugDocks.contains(w) || isExtraMemoryWidget(w)) {
            w->show();
        }
        if (qobject_cast<DisassemblyWidget *>(w)) {
            widgetToFocus = w;
        }
    }
    registerHeight.restoreHeight(registersDock);
    registerWidth.restoreWidth(registersDock);
    if (widgetToFocus) {
        widgetToFocus->raise();
    }
}

void MainWindow::dockOnMainArea(QDockWidget *widget)
{
    QDockWidget *best = nullptr;
    float bestScore = 1;
    // choose best existing area for placing the new widget
    for (auto dock : dockWidgets) {
        if (dock->isHidden() || dock == widget || dock->isFloating()
            ||                              // tabifying onto floating dock using code
                                            // doesn't work well
            dock->parentWidget() != this) { // floating group isn't considered floating
            continue;
        }
        float newScore = 0;
        if (isExtraMemoryWidget(dock)) {
            newScore += 10000000; // prefer existing disssasembly and graph widgets
        }
        newScore += dock->width() * dock->height(); // the bigger the better

        if (newScore > bestScore) {
            bestScore = newScore;
            best = dock;
        }
    }
    if (best) {
        tabifyDockWidget(best, widget);
    } else {
        addDockWidget(Qt::TopDockWidgetArea, widget, Qt::Orientation::Horizontal);
    }
}

void MainWindow::updateSaveProjectAction()
{
    ui->actionSave->setEnabled(!core->cmdRaw("P.").trimmed().isEmpty());
}

void MainWindow::updateWriteUndoRedoActions()
{
    const bool cacheEnabled = core->isIOCacheEnabled();
    const bool hasUndo = cacheEnabled && !core->cmdj("wcj").array().isEmpty();
    ui->actionUndoWrite->setEnabled(hasUndo);
    ui->actionRedoWrite->setEnabled(false);
}

void MainWindow::enableDebugWidgetsMenu(bool enable)
{
    for (QAction *action : ui->menuAddDebugWidgets->actions()) {
        // The breakpoints menu should be available outside of debug
        if (!action->text().contains("Breakpoints")) {
            action->setEnabled(enable);
        }
    }
}

IaitoLayout MainWindow::getViewLayout()
{
    IaitoLayout layout;
    layout.geometry = saveGeometry();
    layout.state = saveState();

    for (auto dock : dockWidgets) {
        QVariantMap properties;
        if (auto iaitoDock = qobject_cast<IaitoDockWidget *>(dock)) {
            properties = iaitoDock->serializeViewProprties();
        }
        layout.viewProperties.insert(dock->objectName(), std::move(properties));
    }
    return layout;
}

IaitoLayout MainWindow::getViewLayout(const QString &name)
{
    auto it = layouts.find(name);
    if (it != layouts.end()) {
        return *it;
    }
    return {};
}

void MainWindow::setViewLayout(const IaitoLayout &layout)
{
    bool isDefault = layout.state.isEmpty() || layout.geometry.isEmpty();
    bool isDebug = Core()->currentlyDebugging;
    bool wasAnimated = isAnimated();

    setAnimated(false);
    clearDefaultSideDockWidths();

    // make a copy to avoid iterating over container from which items are being
    // removed
    auto widgetsToClose = dockWidgets;

    for (auto dock : widgetsToClose) {
        dock->hide();
        dock->setFloating(false); // tabifyDockWidget doesn't work if dock is floating
        removeDockWidget(dock);
        dock->close();
    }

    QStringList docksToCreate;
    if (isDefault) {
        docksToCreate = QStringList{
            DisassemblyWidget::getWidgetType(),
            GraphWidget::getWidgetType(),
            HexdumpWidget::getWidgetType(),
            DecompilerWidget::getWidgetType()};
    } else {
        docksToCreate = layout.viewProperties.keys();
    }

    for (const auto &it : docksToCreate) {
        if (std::none_of(dockWidgets.constBegin(), dockWidgets.constEnd(), [&it](QDockWidget *w) {
                return w->objectName() == it;
            })) {
            auto className = it.split(';').at(0);
            if (widgetTypeToConstructorMap.contains(className)) {
                auto widget = widgetTypeToConstructorMap[className](this);
                widget->setObjectName(it);
                widget->setTransient(true);
                addWidget(widget);
            }
        }
    }

    restoreDocks();

    QList<QDockWidget *> newDocks;

    for (auto dock : dockWidgets) {
        auto properties = layout.viewProperties.find(dock->objectName());
        if (properties != layout.viewProperties.end()) {
            dock->deserializeViewProperties(*properties);
        } else {
            dock->deserializeViewProperties({}); // call with empty properties to reset the widget
            newDocks.push_back(dock);
        }
        dock->ignoreVisibilityStatus(true);
    }

    if (!isDefault) {
        restoreState(layout.state);

        for (auto dock : newDocks) {
            dock->hide(); // hide to prevent dockOnMainArea putting them on each
                          // other
        }
        for (auto dock : newDocks) {
            dockOnMainArea(dock);
            // Show any new docks by default.
            // Showing new builtin docks helps discovering features added in
            // latest release. Installing a new plugin hints that usre will
            // likely want to use it.
            dock->show();
        }
    } else {
        if (isDebug) {
            showDebugDocks();
        } else {
            showZenDocks();
        }
    }

    for (auto dock : dockWidgets) {
        dock->ignoreVisibilityStatus(false);
    }
    if (overviewDock && !overviewDock->getTargetGraphWidget()) {
        for (auto dock : dockWidgets) {
            auto graphDock = qobject_cast<GraphWidget *>(dock);
            if (graphDock && !graphDock->isHidden() && graphDock->getSeekable()->isSynchronized()) {
                overviewDock->setTargetGraphWidget(graphDock);
                break;
            }
        }
    }
    updateVisualNavbarLocation(configuration->getVisualNavbarLocation());
    lastSyncMemoryWidget = nullptr;
    lastMemoryWidget = nullptr;
    setAnimated(wasAnimated);
}

void MainWindow::updateVisualNavbarLocation(Configuration::VisualNavbarLocation location)
{
    if (!visualNavbar) {
        return;
    }

    bool isSuperTop = (location == Configuration::VisualNavbarLocation::SuperTop);
    bool isSuperBottom = (location == Configuration::VisualNavbarLocation::SuperBottom);
    auto area = toolBarAreaFromLocation(location);

    QTimer::singleShot(0, this, [this, area, isSuperTop, isSuperBottom]() {
        if (!visualNavbar) {
            return;
        }

        removeToolBarBreak(visualNavbar);
        removeToolBar(visualNavbar);
        setContentsMargins(0, 0, 0, 0);

        if (isSuperBottom) {
            repositionSuperBottomNavbar();
        } else if (isSuperTop) {
            insertToolBar(ui->mainToolBar, visualNavbar);
            insertToolBarBreak(ui->mainToolBar);
        } else if (area == Qt::TopToolBarArea) {
            addToolBarBreak(area);
            addToolBar(area, visualNavbar);
        } else {
            addToolBar(area, visualNavbar);
        }

        visualNavbar->show();
        visualNavbar->updateGeometry();
        visualNavbar->updateGraphicsScene();
    });
}

void MainWindow::loadLayouts(QSettings &settings)
{
    this->layouts.clear();
    int size = settings.beginReadArray("layouts");
    for (int i = 0; i < size; i++) {
        IaitoLayout layout;
        settings.setArrayIndex(i);
        QString name = settings.value("name", "layout").toString();
        layout.geometry = settings.value("geometry").toByteArray();
        layout.state = settings.value("state").toByteArray();

        auto docks = settings.value("docks").toMap();
        for (auto it = docks.begin(), end = docks.end(); it != end; it++) {
            layout.viewProperties.insert(it.key(), it.value().toMap());
        }

        layouts.insert(name, std::move(layout));
    }
    settings.endArray();
    updateLayoutsMenu();
}

void MainWindow::saveLayouts(QSettings &settings)
{
    settings.beginWriteArray("layouts", layouts.size());
    int arrayIndex = 0;
    for (auto it = layouts.begin(), end = layouts.end(); it != end; ++it, ++arrayIndex) {
        settings.setArrayIndex(arrayIndex);
        settings.setValue("name", it.key());
        auto &layout = it.value();
        settings.setValue("state", layout.state);
        settings.setValue("geometry", layout.geometry);
        QVariantMap properties;
        for (auto it = layout.viewProperties.begin(), end = layout.viewProperties.end(); it != end;
             ++it) {
            properties.insert(it.key(), it.value());
        }
        settings.setValue("docks", properties);
    }
    settings.endArray();
}

void MainWindow::on_actionDefault_triggered()
{
    // Ensure toolbars come back — otherwise a layout reset would leave a
    // toolbar the user hid via its context menu still hidden.
    ui->mainToolBar->show();
    if (visualNavbar) {
        visualNavbar->show();
    }
    if (core->currentlyDebugging) {
        layouts[LAYOUT_DEBUG] = {};
        setViewLayout(layouts[LAYOUT_DEBUG]);
    } else {
        layouts[LAYOUT_DEFAULT] = {};
        setViewLayout(layouts[LAYOUT_DEFAULT]);
    }
}

/**
 * @brief MainWindow::on_actionNew_triggered
 * Open a new Iaito session.
 */
void MainWindow::on_actionNew_triggered()
{
    // Create a new Iaito process
    static_cast<IaitoApplication *>(qApp)->launchNewInstance();
}

void MainWindow::on_actionManageAddressSpace_triggered()
{
    if (auto *dialog = findChild<AddressSpaceManagerDialog *>()) {
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        return;
    }

    auto *dialog = new AddressSpaceManagerDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::on_actionSave_triggered()
{
    saveProject();
}

void MainWindow::on_actionSaveAs_triggered()
{
    saveProjectAs();
}

void MainWindow::on_actionRun_Script_triggered()
{
    runScriptFromDialog(tr("Select radare2 script"), tr("Radare scripts (*.r2);;All files (*)"));
}

void MainWindow::on_actionRunR2jsScript_triggered()
{
    runScriptFromDialog(tr("Select r2js script"), tr("r2js scripts (*.r2.js);;All files (*)"));
}

void MainWindow::on_actionScripts_triggered()
{
    if (auto *dialog = findChild<ScriptManagerDialog *>()) {
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        return;
    }

    auto *dialog = new ScriptManagerDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::runScriptFromDialog(const QString &caption, const QString &filter)
{
    const QString fileName = QFileDialog::getOpenFileName(
        this, caption, Config()->getRecentFolder(), filter, nullptr, QFILEDIALOG_FLAGS);
    if (fileName.isEmpty()) {
        return;
    }

    Config()->setRecentFolder(QFileInfo(fileName).absolutePath());
    runScriptFile(fileName);
}

void MainWindow::runScriptFile(const QString &fileName)
{
    if (fileName.isEmpty()) {
        return;
    }

    addRecentScript(fileName);

    RunScriptTask *runScriptTask = new RunScriptTask();
    runScriptTask->setFileName(QDir::toNativeSeparators(fileName));

    AsyncTask::Ptr runScriptTaskPtr(runScriptTask);

    AsyncTaskDialog *taskDialog = new AsyncTaskDialog(runScriptTaskPtr, this);
    taskDialog->setInterruptOnClose(true);
    taskDialog->setAttribute(Qt::WA_DeleteOnClose);
    taskDialog->show();

    Core()->getAsyncTaskManager()->start(runScriptTaskPtr);
}

void MainWindow::addRecentScript(const QString &fileName)
{
    QStringList files = QSettings().value(recentScriptsSettingsKey).toStringList();
    files.removeAll(fileName);
    files.prepend(fileName);
    while (files.size() > maxRecentScripts) {
        files.removeLast();
    }
    QSettings().setValue(recentScriptsSettingsKey, files);
}

void MainWindow::rebuildRecentScriptsMenu()
{
    for (QAction *action : ui->menuRun->actions()) {
        if (action->property(recentScriptDynamicActionProperty).toBool()) {
            ui->menuRun->removeAction(action);
            action->deleteLater();
        }
    }

    const QStringList files = QSettings().value(recentScriptsSettingsKey).toStringList();
    for (const QString &fileName : files) {
        QFileInfo fileInfo(fileName);
        QAction *action = new QAction(fileInfo.fileName(), ui->menuRun);
        setAppMenuIcon(this, action, AppMenuIcon::Script, QColor(67, 160, 71));
        action->setToolTip(QDir::toNativeSeparators(fileName));
        action->setStatusTip(QDir::toNativeSeparators(fileName));
        action->setProperty(recentScriptDynamicActionProperty, true);
        action->setEnabled(fileInfo.exists());
        connect(action, &QAction::triggered, this, [this, fileName]() { runScriptFile(fileName); });
        ui->menuRun->insertAction(ui->actionScripts, action);
    }
    fillTopLevelMenuIconGaps();
}

/**
 * @brief MainWindow::on_actionOpen_triggered
 * Open a file as in "load (add) a file in current session".
 */
void MainWindow::on_actionMap_triggered()
{
    MapFileDialog dialog(this);
    dialog.exec();
}

void MainWindow::toggleResponsive(bool maybe)
{
    this->responsive = maybe;
    // Save options in settings
    QSettings settings;
    settings.setValue("responsive", this->responsive);
}

void MainWindow::on_actionTabs_on_Top_triggered()
{
    this->on_actionTabs_triggered();
}

void MainWindow::on_actionReset_settings_triggered()
{
    QMessageBox::StandardButton ret = (QMessageBox::StandardButton) QMessageBox::question(
        this,
        APPNAME,
        tr("Do you really want to clear all settings?"),
        QMessageBox::Ok | QMessageBox::Cancel);
    if (ret == QMessageBox::Ok) {
        Config()->resetAll();
        readSettings();
        setViewLayout(getViewLayout(Core()->currentlyDebugging ? LAYOUT_DEBUG : LAYOUT_DEFAULT));
    }
}

void MainWindow::on_actionQuit_triggered()
{
    close();
}

void MainWindow::on_actionBackward_triggered()
{
    core->seekPrev();
}

void MainWindow::on_actionForward_triggered()
{
    core->seekNext();
}

void MainWindow::on_actionDisasAdd_comment_triggered()
{
    CommentsDialog c(this);
    c.exec();
}

void MainWindow::on_actionHighlight_triggered()
{
    bool ok;
    QString current = Core()->getConfig("scr.highlight");
    QString text = QInputDialog::getText(
        this,
        tr("Highlight"),
        tr("Text to highlight (leave empty to clear):"),
        QLineEdit::Normal,
        current,
        &ok);
    if (ok) {
        Core()->setConfig("scr.highlight", text);
        refreshAll();
    }
}

void MainWindow::on_actionRefresh_contents_triggered()
{
    refreshAll();
}

void MainWindow::on_actionSettings_triggered()
{
    if (!findChild<SettingsDialog *>()) {
        auto dialog = new SettingsDialog(this);
        dialog->show();
    }
}

void MainWindow::on_actionAnalysisSettings_triggered()
{
    auto *dialog = findChild<SettingsDialog *>();
    if (!dialog) {
        dialog = new SettingsDialog(this);
    }
    dialog->showSection(SettingsDialog::Section::Analysis);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::on_actionStart_Web_Server_triggered(bool checked)
{
    if (checked) {
        // Start the webserver using r2's background server mode
        QString port = Core()->getConfig("http.port");
        QString bind = Core()->getConfig("http.bind");

        // Probe the port first: r2's `=h&` spawns a background task that
        // retries on bind failures, so we can't rely on its return value and
        // a port-in-use condition would spam errors indefinitely.
        {
            QTcpServer probe;
            QHostAddress addr = (bind == QLatin1String("public")) ? QHostAddress::Any
                                                                  : QHostAddress(bind);
            if (addr.isNull()) {
                addr = QHostAddress::LocalHost;
            }
            if (!probe.listen(addr, port.toUShort())) {
                ui->actionStart_Web_Server->setChecked(false);
                QMessageBox::warning(
                    this,
                    tr("Web Server"),
                    tr("Cannot bind web server to %1:%2\n%3").arg(bind, port, probe.errorString()));
                return;
            }
            probe.close();
        }

        QString result = Core()->cmd("=h&");
        if (result.contains("error") || result.contains("Cannot")) {
            webserverRunning = false;
            ui->actionStart_Web_Server->setChecked(false);
            QMessageBox::warning(
                this,
                tr("Web Server"),
                tr("Failed to start web server on %1:%2\n%3").arg(bind, port, result.trimmed()));
            return;
        }
        webserverRunning = true;
        ui->actionStart_Web_Server->setText(tr("Stop web server"));
        if (webserverButton) {
            webserverButton->setToolTip(
                tr("Web server running on %1:%2 (click to open browser)").arg(bind, port));
            webserverButton->show();
        }
        statusBar()->showMessage(tr("Web server started on %1:%2").arg(bind, port), 5000);
    } else {
        // Stop the webserver: send a dummy request to unblock accept(),
        // then tell r2 to tear down the listener.
        QString port = Core()->getConfig("http.port");
        QString bind = Core()->getConfig("http.bind");
        auto *kick = new QNetworkAccessManager(this);
        QNetworkRequest req(QUrl(QStringLiteral("http://%1:%2/").arg(bind, port)));
        auto *reply = kick->get(req);
        connect(reply, &QNetworkReply::finished, kick, &QObject::deleteLater);

        Core()->cmd("=h-");
        webserverRunning = false;
        ui->actionStart_Web_Server->setText(tr("Start web server"));
        if (webserverButton) {
            webserverButton->setToolTip(tr("Open web server in browser"));
            webserverButton->hide();
        }
    }
}

void MainWindow::on_actionTabs_triggered()
{
    tabsOnTop = !tabsOnTop;
    setTabLocation();
}

void MainWindow::on_actionAbout_triggered()
{
    AboutDialog *a = new AboutDialog(this);
    a->setAttribute(Qt::WA_DeleteOnClose);
    a->open();
}

void MainWindow::on_actionIssue_triggered()
{
    openIssue();
}

void MainWindow::on_actionR2pm_triggered()
{
    PackageManagerDialog dlg(this);
    dlg.exec();
}
// Launch via Settings menu
void MainWindow::on_actionPackageManagerPrefs_triggered()
{
    PackageManagerDialog dlg(this);
    dlg.exec();
}

void MainWindow::websiteClicked()
{
    QDesktopServices::openUrl(QUrl("https://www.radare.org"));
}

void MainWindow::fortuneClicked()
{
    FortuneDialog dialog;
    dialog.setWindowTitle("Fortune Message");
    dialog.resize(300, 150);
    dialog.exec();
}

void MainWindow::bookClicked()
{
    QDesktopServices::openUrl(QUrl("https://book.rada.re"));
}

void MainWindow::checkForUpdatesClicked()
{
    QDesktopServices::openUrl(QUrl("https://github.com/radareorg/iaito/releases"));
}

void MainWindow::on_actionRefresh_Panels_triggered()
{
    this->refreshAll();
}

/**
 * @brief Show analysis options for the currently opened file and re-run the selected analysis.
 */
void MainWindow::on_actionAnalyze_triggered()
{
    InitialOptions options;
    options.filename = getFilename();
    options.arch = core->getConfig("asm.arch");
    options.cpu = core->getConfig("asm.cpu");
    options.bits = core->getConfig("asm.bits").toInt();
    options.os = core->getConfig("asm.os");
    options.analVars = core->getConfigb("anal.vars");
    options.endian = core->getConfigb("cfg.bigendian") ? InitialOptions::Endianness::Big
                                                       : InitialOptions::Endianness::Little;
    options.writeEnabled = ioModesController.getIOMode() == IOModesController::Mode::WRITE;
    options.loadBinCache = core->getConfigb("bin.cache");
    options.demangle = core->getConfigb("bin.demangle");

    displayInitialOptionsDialog(options, false, true);
}

void MainWindow::on_actionAnalyzeFunction_triggered()
{
    core->cmdRaw("af");
    refreshAll();
}

void MainWindow::on_actionAutonameAll_triggered()
{
    core->cmdRaw("aan");
    refreshAll();
}

void MainWindow::on_actionPatchInstruction_triggered()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }

    const RVA off = core->getOffset();
    EditInstructionDialog dialog(EDIT_TEXT, this);
    dialog.setWindowTitle(tr("Patch Instruction at %1").arg(RAddressString(off)));

    const QString oldInstructionOpcode = core->getInstructionOpcode(off);
    dialog.setInstruction(oldInstructionOpcode);

    if (dialog.exec()) {
        const QString newInstructionOpcode = dialog.getInstruction();
        if (newInstructionOpcode != oldInstructionOpcode) {
            core->editInstruction(off, newInstructionOpcode);
        }
    }
}

void MainWindow::on_actionPatchString_triggered()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }

    const RVA off = core->getOffset();
    QString oldString = core->getString(off);
    while (oldString.endsWith(QLatin1Char('\n')) || oldString.endsWith(QLatin1Char('\r'))) {
        oldString.chop(1);
    }

    bool ok = false;
    const QString newString = QInputDialog::getText(
        this,
        tr("Patch String at %1").arg(RAddressString(off)),
        tr("String:"),
        QLineEdit::Normal,
        oldString,
        &ok);
    if (ok && !newString.isEmpty() && newString != oldString) {
        core->editBytes(off, QString::fromLatin1(newString.toUtf8().toHex()));
    }
}

void MainWindow::on_actionPatchBytes_triggered()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }

    const RVA off = core->getOffset();
    EditInstructionDialog dialog(EDIT_BYTES, this);
    dialog.setWindowTitle(tr("Patch Bytes at %1").arg(RAddressString(off)));

    const QString oldBytes = core->getInstructionBytes(off);
    dialog.setInstruction(oldBytes);

    if (dialog.exec()) {
        const QString bytes = dialog.getInstruction();
        if (bytes != oldBytes) {
            core->editBytes(off, bytes);
        }
    }
}

void MainWindow::on_actionImportPDB_triggered()
{
    QFileDialog dialog(this);
    dialog.setWindowTitle(tr("Select PDB file"));
    dialog.setNameFilters({tr("PDB file (*.pdb)"), tr("All files (*)")});

    if (!dialog.exec()) {
        return;
    }

    const QString &pdbFile = QDir::toNativeSeparators(dialog.selectedFiles().first());

    if (!pdbFile.isEmpty()) {
        core->loadPDB(pdbFile);
        core->message(tr("%1 loaded.").arg(pdbFile));
        this->refreshAll();
    }
}

void MainWindow::on_actionImportSymbols_triggered()
{
    QFileDialog dialog(this);
    dialog.setWindowTitle(tr("Select binary object file"));
    dialog.setNameFilter(tr("All files (*)"));

    if (!dialog.exec()) {
        return;
    }

    const QString objectFile = dialog.selectedFiles().first();

    if (!objectFile.isEmpty()) {
        core->cmdRaw(QStringLiteral("obf %1").arg(r2QuotedFileArg(objectFile)));
        core->message(tr("%1 imported.").arg(QDir::toNativeSeparators(objectFile)));
        refreshAll();
    }
}

void MainWindow::on_actionImportDWARF_triggered()
{
    QFileDialog dialog(this);
    dialog.setWindowTitle(tr("Select DWARF information file"));
    dialog.setNameFilter(tr("All files (*)"));

    if (!dialog.exec()) {
        return;
    }

    const QString objectFile = dialog.selectedFiles().first();
    if (!objectFile.isEmpty()) {
        core->cmdRaw(QStringLiteral("obf %1").arg(r2QuotedFileArg(objectFile)));
        core->message(tr("%1 imported.").arg(QDir::toNativeSeparators(objectFile)));
        refreshAll();
    }
}

void MainWindow::on_actionImportFLIRT_triggered()
{
    QFileDialog dialog(this);
    dialog.setWindowTitle(tr("Select FLIRT signature file"));
    dialog.setNameFilters({tr("FLIRT signature files (*.sig)"), tr("All files (*)")});

    if (!dialog.exec()) {
        return;
    }

    const QString signatureFile = dialog.selectedFiles().first();
    if (!signatureFile.isEmpty()) {
        core->cmdRaw(QStringLiteral("zfs %1").arg(r2QuotedFileArg(signatureFile)));
        core->message(tr("%1 imported.").arg(QDir::toNativeSeparators(signatureFile)));
        refreshAll();
    }
}

void MainWindow::on_actionImportProjectArchive_triggered()
{
    QFileDialog dialog(this, tr("Import project archive"));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilters({tr("Project archives (*.zrp *.zip)"), tr("All files (*)")});

    if (!dialog.exec()) {
        return;
    }

    const QString archiveFile = dialog.selectedFiles().first();
    if (!archiveFile.isEmpty()) {
        core->cmdRaw(QStringLiteral("Pzi %1").arg(r2QuotedFileArg(archiveFile)));
        core->message(tr("%1 imported.").arg(QDir::toNativeSeparators(archiveFile)));
    }
}

void MainWindow::on_actionExport_as_code_triggered()
{
    QStringList filters;
    QMap<QString, QString> cmdMap;

    filters << tr("C uin8_t array (*.c)");
    cmdMap[filters.last()] = "pc";
    filters << tr("C uin16_t array (*.c)");
    cmdMap[filters.last()] = "pch";
    filters << tr("C uin32_t array (*.c)");
    cmdMap[filters.last()] = "pcw";
    filters << tr("C uin64_t array (*.c)");
    cmdMap[filters.last()] = "pcd";
    filters << tr("C string (*.c)");
    cmdMap[filters.last()] = "pcs";
    filters << tr("Shell-script that reconstructs the bin (*.sh)");
    cmdMap[filters.last()] = "pcS";
    filters << tr("JSON array (*.json)");
    cmdMap[filters.last()] = "pcj";
    filters << tr("JavaScript array (*.js)");
    cmdMap[filters.last()] = "pcJ";
    filters << tr("Python array (*.py)");
    cmdMap[filters.last()] = "pcp";
    filters << tr("Print 'wx' r2 commands (*.r2)");
    cmdMap[filters.last()] = "pc*";
    filters << tr("GAS .byte blob (*.asm, *.s)");
    cmdMap[filters.last()] = "pca";
    filters << tr(".bytes with instructions in comments (*.txt)");
    cmdMap[filters.last()] = "pcA";
    filters << tr("Disassembly of the current Section (*.asm)");
    cmdMap[filters.last()] = "pD $SS@e:asm.lines=0@$$@e:emu.str=true@e:scr.color=0";

    QFileDialog dialog(this, tr("Export as code"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(filters);
    dialog.selectFile("dump");
    dialog.setDefaultSuffix("c");
    if (!dialog.exec())
        return;

    QFile file(dialog.selectedFiles()[0]);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Can't open file";
        return;
    }
    bool pamode = true;
    QTextStream fileOut(&file);
    QString &cmd = cmdMap[dialog.selectedNameFilter()];
    if (cmd.contains("@")) {
        pamode = false;
    }
    if (pamode) {
        TempConfig tempConfig; // TODO Use RConfigHold
        tempConfig.set("io.va", false);
        QString &cmd = cmdMap[dialog.selectedNameFilter()];
        // Use cmd because cmdRaw would not handle such input
        fileOut << Core()->cmd(cmd + " $s @ 0");
    } else {
        // Use cmd because cmdRaw would not handle such input
        fileOut << Core()->cmd(cmd);
    }
}

void MainWindow::on_actionDump_triggered()
{
    DumpDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QString filePath = dialog.getFilePath();
    RVA address = dialog.getAddress();
    ut64 length = dialog.getLength();
    bool useVa = dialog.useVirtualAddressing();

    TempConfig tempConfig;
    tempConfig.set("io.va", useVa);
    Core()->cmdRawAt(QStringLiteral("wtf %1 %2").arg(filePath).arg(length), address);
    Core()->message(tr("Dumped %1 bytes from %2 to %3")
                        .arg(length)
                        .arg(RAddressString(address))
                        .arg(QDir::toNativeSeparators(filePath)));
}

void MainWindow::on_actionExportDWARF_triggered()
{
    QFileDialog dialog(this, tr("Export DWARF information"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters({tr("Text files (*.txt)"), tr("All files (*)")});
    dialog.selectFile("dwarf-info.txt");
    dialog.setDefaultSuffix("txt");

    if (!dialog.exec()) {
        return;
    }

    QFile file(dialog.selectedFiles().first());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot open file for writing."));
        return;
    }

    QTextStream fileOut(&file);
    fileOut << Core()->cmd("id");
}

void MainWindow::on_actionExportProjectArchive_triggered()
{
    QString projectName = core->getConfig("prj.name");
    if (projectName.isEmpty()) {
        if (!saveProjectAs()) {
            return;
        }
        projectName = core->getConfig("prj.name");
    }
    if (projectName.isEmpty()) {
        return;
    }

    QFileDialog dialog(this, tr("Export project archive"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters({tr("Project archives (*.zrp)"), tr("All files (*)")});
    dialog.selectFile(projectName + QStringLiteral(".zrp"));
    dialog.setDefaultSuffix("zrp");

    if (!dialog.exec()) {
        return;
    }

    const QString archiveFile = dialog.selectedFiles().first();
    if (!archiveFile.isEmpty()) {
        core->cmdRaw(QStringLiteral("Pze %1 %2")
                         .arg(IaitoCore::sanitizeStringForCommand(projectName))
                         .arg(r2QuotedFileArg(archiveFile)));
        core->message(tr("Project exported to %1").arg(QDir::toNativeSeparators(archiveFile)));
    }
}

void MainWindow::on_actionGrouped_dock_dragging_triggered(bool checked)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 7, 0)
    auto options = dockOptions();
    options.setFlag(QMainWindow::DockOption::GroupedDragging, checked);
    setDockOptions(options);
#else
    Q_UNUSED(checked);
#endif
}

void MainWindow::seekToFunctionLastInstruction()
{
    Core()->seek(Core()->getLastFunctionInstruction(Core()->getOffset()));
}

void MainWindow::seekToFunctionStart()
{
    Core()->seek(Core()->getFunctionStart(Core()->getOffset()));
}

void MainWindow::projectSaved(bool successfully, const QString &name)
{
    updateSaveProjectAction();
    if (successfully)
        core->message(tr("Project saved: %1").arg(name));
    else
        core->message(tr("Failed to save project: %1").arg(name));
}

void MainWindow::toggleDebugView()
{
    MemoryWidgetType memType = getMemoryWidgetTypeToRestore();
    if (Core()->currentlyDebugging) {
        layouts[LAYOUT_DEFAULT] = getViewLayout();
        setViewLayout(getViewLayout(LAYOUT_DEBUG));
        enableDebugWidgetsMenu(true);
    } else {
        layouts[LAYOUT_DEBUG] = getViewLayout();
        setViewLayout(getViewLayout(LAYOUT_DEFAULT));
        enableDebugWidgetsMenu(false);
    }
    showMemoryWidget(memType);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    switch (event->button()) {
    case Qt::BackButton:
        core->seekPrev();
        break;
    case Qt::ForwardButton:
        core->seekNext();
        break;
    default:
        break;
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        int key = keyEvent->key();
        if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
            QTimer::singleShot(0, this, &MainWindow::refreshAll);
        }
    }
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        int key = keyEvent->key();
        if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
            int idx = key - Qt::Key_F1 + 1;
            QString configKey = QStringLiteral("key.f%1").arg(idx);
            QString cmd = core->getConfig(configKey);
            if (!cmd.isEmpty()) {
                core->cmd(cmd);
                return true;
            }
        }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::ForwardButton || mouseEvent->button() == Qt::BackButton) {
            mousePressEvent(mouseEvent);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange
        || event->type() == QEvent::PaletteChange) {
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
        QMetaObject::invokeMethod(Config(), "refreshFont", Qt::ConnectionType::QueuedConnection);
#else
        QMetaObject::invokeMethod(
            Config(), &Configuration::refreshFont, Qt::ConnectionType::QueuedConnection);
#endif
    }
    if (event->type() == QEvent::Resize) {
        repositionSuperBottomNavbar();
    }
    return QMainWindow::event(event);
}

void MainWindow::repositionSuperBottomNavbar()
{
    if (!visualNavbar) {
        return;
    }
    if (Config()->getVisualNavbarLocation() != Configuration::VisualNavbarLocation::SuperBottom) {
        return;
    }
    int h = visualNavbar->sizeHint().height();
    if (h <= 0) {
        h = 20;
    }
    setContentsMargins(0, 0, 0, h);
    visualNavbar->setGeometry(0, height() - h, width(), h);
    visualNavbar->raise();
    visualNavbar->show();
}

/**
 * @brief Show a warning message box.
 */
void MainWindow::messageBoxWarning(QString title, QString message)
{
    QMessageBox mb(this);
    mb.setIcon(QMessageBox::Warning);
    mb.setStandardButtons(QMessageBox::Ok);
    mb.setWindowTitle(title);
    mb.setText(message);
    mb.exec();
}

/**
 * @brief When theme changed, change icons which have a special version for the
 * theme.
 */
void MainWindow::chooseThemeIcons()
{
    // List of QActions which have alternative icons in different themes
    const QList<QPair<void *, QString>> kSupportedIconsNames{
        {ui->actionForward, QStringLiteral("arrow_right.svg")},
        {ui->actionBackward, QStringLiteral("arrow_left.svg")},
    };

    // Set the correct icon for the QAction
    qhelpers::setThemeIcons(kSupportedIconsNames, [](void *obj, const QIcon &icon) {
        static_cast<QAction *>(obj)->setIcon(icon);
    });
}

void MainWindow::onZoomIn()
{
    Config()->setZoomFactor(Config()->getZoomFactor() + 0.1);
}

void MainWindow::onZoomOut()
{
    Config()->setZoomFactor(Config()->getZoomFactor() - 0.1);
}

void MainWindow::onZoomReset()
{
    Config()->setZoomFactor(1.0);
}

QMenu *MainWindow::getContextMenuExtensions(ContextMenuType type)
{
    switch (type) {
    case ContextMenuType::Disassembly:
        return disassemblyContextMenuExtensions;
    case ContextMenuType::Addressable:
        return addressableContextMenuExtensions;
    default:
        return nullptr;
    }
}

void MainWindow::setAvailableIOModeOptions()
{
    switch (ioModesController.getIOMode()) {
    case IOModesController::Mode::WRITE:
        ui->actionWriteMode->setChecked(true);
        break;
    case IOModesController::Mode::CACHE:
        ui->actionCacheMode->setChecked(true);
        break;
    default:
        ui->actionReadOnly->setChecked(true);
    }
}
