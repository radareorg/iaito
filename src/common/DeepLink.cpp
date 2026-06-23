#include "DeepLink.h"

#include "MemoryDockWidget.h"
#include "common/InitialOptions.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"

#include <memory>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>

namespace {

// Parsed query parameters shared by every handler.
struct Params
{
    QString addr;
    QString view;
    QString size;
    QString arch;
    QString cpu;
    QString os;
    QString bits;
    QString endian;
    QString base;
};

// Strips the single leading slash QUrl keeps in the path of authority URIs.
QString stripLeadingSlash(const QString &path)
{
    return path.startsWith(QLatin1Char('/')) ? path.mid(1) : path;
}

void applyAnalParams(InitialOptions &options, const Params &p)
{
    if (!p.arch.isEmpty()) {
        options.arch = p.arch;
    }
    if (!p.cpu.isEmpty()) {
        options.cpu = p.cpu;
    }
    if (!p.os.isEmpty()) {
        options.os = p.os;
    }
    if (!p.bits.isEmpty()) {
        options.bits = p.bits.toInt();
    }
    if (p.endian.compare(QStringLiteral("big"), Qt::CaseInsensitive) == 0) {
        options.endian = InitialOptions::Endianness::Big;
    } else if (p.endian.compare(QStringLiteral("little"), Qt::CaseInsensitive) == 0) {
        options.endian = InitialOptions::Endianness::Little;
    }
    if (!p.base.isEmpty()) {
        options.binLoadAddr = Core()->math(p.base);
        options.mapAddr = options.binLoadAddr;
    }
}

void showView(MainWindow *main, const QString &view)
{
    const QString v = view.toLower();
    if (v == QStringLiteral("disasm")) {
        main->showMemoryWidget(MemoryWidgetType::Disassembly);
    } else if (v == QStringLiteral("graph")) {
        main->showMemoryWidget(MemoryWidgetType::Graph);
    } else if (v == QStringLiteral("hex")) {
        main->showMemoryWidget(MemoryWidgetType::Hexdump);
    } else if (v == QStringLiteral("decompiler")) {
        main->showMemoryWidget(MemoryWidgetType::Decompiler);
    } else {
        // strings, imports, exports, functions and any other dock panel
        main->focusPanelByName(v);
    }
}

void applyLocation(MainWindow *main, const Params &p)
{
    if (!p.size.isEmpty()) {
        // Resolve numerically so size= can never inject extra r2 commands.
        Core()->cmdRaw(QStringLiteral("b %1").arg(Core()->math(p.size)));
    }
    if (!p.addr.isEmpty()) {
        Core()->seek(p.addr);
    }
    if (!p.view.isEmpty()) {
        showView(main, p.view);
    } else if (!p.addr.isEmpty()) {
        Core()->seekAndShow(Core()->getOffset());
    }
}

// Arms a one-shot uiReady handler that applies addr/view/size once the open
// that follows finishes loading. Call this only right before an open is issued,
// so a parse/lookup failure never leaves a connection that misfires on a later
// unrelated open.
void scheduleApply(MainWindow *main, const Params &p)
{
    if (p.addr.isEmpty() && p.view.isEmpty() && p.size.isEmpty()) {
        return;
    }
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = QObject::connect(main, &MainWindow::uiReady, main, [main, p, conn]() {
        QObject::disconnect(*conn);
        applyLocation(main, p);
    });
}

void openFile(MainWindow *main, const QString &path, const Params &p)
{
    InitialOptions options;
    options.filename = path;
    applyAnalParams(options, p);
    scheduleApply(main, p);
    main->openNewFile(options, true);
}

void openProject(MainWindow *main, const QString &name, const Params &p)
{
    scheduleApply(main, p);
    main->openProject(name);
}

void openBytes(MainWindow *main, const QString &hex, const Params &p)
{
    QString clean = hex;
    clean.remove(QRegularExpression(QStringLiteral("[^0-9a-fA-F]")));
    if (clean.size() < 2) {
        QMessageBox::critical(
            main, QObject::tr("Deep link"), QObject::tr("Empty or invalid bytes payload."));
        return;
    }
    const int nbytes = clean.size() / 2;
    InitialOptions options;
    options.filename = QStringLiteral("malloc://%1").arg(nbytes);
    options.shellcode = clean.left(nbytes * 2);
    applyAnalParams(options, p);
    scheduleApply(main, p);
    main->openNewFile(options, true);
}

// Looks up the sample by sha256 in the local index and opens it. Registering or
// downloading unknown samples is intentionally out of scope (see DEEPLINK.md).
void openHash(MainWindow *main, const QString &hash, const Params &p)
{
    const QString h = hash.toLower();
    const QString index = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                              .filePath(QStringLiteral("samples.json"));
    QString path;
    QFile f(index);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        path = obj.value(h).toString();
    }
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        openFile(main, path, p);
        return;
    }
    QMessageBox::warning(
        main,
        QObject::tr("Deep link"),
        QObject::tr("No local sample is registered for SHA256:\n%1\n\nRegister it in %2 to open it.")
            .arg(h, index));
}

} // namespace

namespace DeepLink {

bool isDeepLink(const QString &arg)
{
    return arg.startsWith(QStringLiteral("iaito://"), Qt::CaseInsensitive);
}

bool handle(MainWindow *main, const QString &uri)
{
    if (main == nullptr || !isDeepLink(uri)) {
        return false;
    }
    const QUrl url(uri, QUrl::TolerantMode);
    if (url.scheme().compare(QStringLiteral("iaito"), Qt::CaseInsensitive) != 0) {
        return false;
    }

    Params p;
    const QUrlQuery q(url);
    p.addr = q.queryItemValue(QStringLiteral("addr"));
    p.view = q.queryItemValue(QStringLiteral("view"));
    p.size = q.queryItemValue(QStringLiteral("size"));
    p.arch = q.queryItemValue(QStringLiteral("arch"));
    p.cpu = q.queryItemValue(QStringLiteral("cpu"));
    p.os = q.queryItemValue(QStringLiteral("os"));
    p.bits = q.queryItemValue(QStringLiteral("bits"));
    p.endian = q.queryItemValue(QStringLiteral("endian"));
    p.base = q.queryItemValue(QStringLiteral("base"));

    const QString host = url.host().toLower();
    const QString path = url.path();

    if (host.isEmpty() || host == QStringLiteral("file")) {
        // iaito:///bin/ls and iaito://file/bin/ls both map to /bin/ls
        if (path.isEmpty()) {
            return false;
        }
        openFile(main, path, p);
    } else if (host == QStringLiteral("project")) {
        openProject(main, stripLeadingSlash(path), p);
    } else if (host == QStringLiteral("sha256")) {
        openHash(main, stripLeadingSlash(path), p);
    } else if (host == QStringLiteral("bytes")) {
        openBytes(main, stripLeadingSlash(path), p);
    } else {
        return false;
    }
    return true;
}

} // namespace DeepLink
