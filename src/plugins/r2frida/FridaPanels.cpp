#include "FridaPanels.h"

#include "FridaSession.h"
#include "FridaWidget.h"
#include "core/Iaito.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

QString formatSize(quint64 size)
{
    if (size >= (1024ull * 1024ull * 1024ull)) {
        return QString::number(size / (1024.0 * 1024.0 * 1024.0), 'f', 1) + QStringLiteral(" GB");
    }
    if (size >= (1024ull * 1024ull)) {
        return QString::number(size / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
    }
    if (size >= 1024ull) {
        return QString::number(size / 1024.0, 'f', 1) + QStringLiteral(" KB");
    }
    return QString::number(size) + QStringLiteral(" B");
}

QStandardItem *textItem(const QString &text)
{
    auto *item = new QStandardItem(text);
    item->setEditable(false);
    return item;
}

QStandardItem *addressItem(const QString &text, quint64 runtime)
{
    auto *item = textItem(text);
    item->setData(QVariant::fromValue(runtime), Qt::UserRole);
    return item;
}

quint64 roleAddress(const QModelIndex &index)
{
    return index.data(Qt::UserRole).toULongLong();
}

void host(QWidget *page, QWidget *child)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(child);
}

void loadJava(FridaSession *session, QPlainTextEdit *view)
{
    if (!session->isAttached()) {
        view->setPlainText(QObject::tr("Not attached."));
        return;
    }
    view->setPlainText(QObject::tr("Loading..."));
    const QString source = QStringLiteral(
        "if (!Java.available) { console.log('Java runtime is not available'); }\n"
        "else Java.perform(function () {\n"
        "  var n = 0;\n"
        "  Java.enumerateLoadedClasses({\n"
        "    onMatch: function (name) { if (n++ < 2000) console.log(name); },\n"
        "    onComplete: function () { if (n >= 2000) console.log('... truncated'); }\n"
        "  });\n"
        "});\n");
    const QString path = session->writeScript(source);
    if (path.isEmpty()) {
        view->setPlainText(QObject::tr("Could not write the Java script."));
        return;
    }
    session->command(QStringLiteral(". ") + path, [view](const QString &output) {
        view->setPlainText(output);
    });
}

void showCommand(FridaSession *session, const QString &command, QPlainTextEdit *view)
{
    if (!session->isAttached()) {
        view->setPlainText(QObject::tr("Not attached."));
        return;
    }
    view->setPlainText(QObject::tr("Loading..."));
    session->command(command, [view](const QString &output) {
        QString text = output;
        if (text.size() > 1000000) {
            text.truncate(1000000);
            text += QObject::tr("\n... truncated");
        }
        view->setPlainText(text);
    });
}

class FridaOutputDock : public QWidget
{
public:
    FridaOutputDock(
        const QString &title,
        const QString &objectName,
        FridaSession *fridaSession,
        const std::function<void(QPlainTextEdit *)> &load)
        : QWidget(nullptr)
        , session(fridaSession)
    {
        setObjectName(objectName);
        setWindowTitle(title);
        auto *root = new QWidget(this);
        auto *layout = new QVBoxLayout(root);
        auto *refresh = new QPushButton(tr("Refresh"), root);
        view = new QPlainTextEdit(root);
        view->setReadOnly(true);
        layout->addWidget(refresh);
        layout->addWidget(view);
        host(this, root);
        connect(refresh, &QPushButton::clicked, this, [this, load]() { load(view); });
        connect(session, &FridaSession::changed, this, [this]() {
            if (!session->isAttached()) {
                view->clear();
            }
        });
    }

private:
    FridaSession *session = nullptr;
    QPlainTextEdit *view = nullptr;
};

class FridaModulesDock : public QWidget
{
public:
    FridaModulesDock(FridaSession *fridaSession)
        : QWidget(nullptr)
        , session(fridaSession)
    {
        setObjectName(QStringLiteral("FridaModulesDock"));
        setWindowTitle(tr("Modules"));
        model = new QStandardItemModel(this);
        model->setHorizontalHeaderLabels({tr("Name"), tr("Base"), tr("Size"), tr("Path")});
        proxy = new QSortFilterProxyModel(this);
        proxy->setSourceModel(model);
        proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        proxy->setFilterKeyColumn(-1);

        auto *root = new QWidget(this);
        auto *layout = new QVBoxLayout(root);
        auto *row = new QHBoxLayout();
        auto *filter = new QLineEdit(root);
        filter->setPlaceholderText(tr("Filter"));
        auto *mode = new QComboBox(root);
        mode->addItem(tr("Runtime"), int(FridaAddressMode::Runtime));
        mode->addItem(tr("Static"), int(FridaAddressMode::Static));
        mode->addItem(tr("Module + offset"), int(FridaAddressMode::ModuleOffset));
        row->addWidget(filter, 1);
        row->addWidget(mode);
        view = new QTreeView(root);
        view->setModel(proxy);
        view->setRootIsDecorated(false);
        view->setAlternatingRowColors(true);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        view->header()->setStretchLastSection(true);
        view->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        layout->addLayout(row);
        layout->addWidget(view);
        host(this, root);

        connect(
            filter, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
        connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, mode](int index) {
            session->setAddressMode(static_cast<FridaAddressMode>(mode->itemData(index).toInt()));
        });
        connect(session, &FridaSession::changed, this, [this]() { refill(); });
        connect(view, &QTreeView::activated, this, [this](const QModelIndex &index) {
            fridaSeekRuntime(session, roleAddress(index.sibling(index.row(), 0)));
        });
        connect(view, &QTreeView::customContextMenuRequested, this, [this](const QPoint &pos) {
            const QModelIndex index = view->indexAt(pos);
            if (!index.isValid()) {
                return;
            }
            const QModelIndex nameIndex = index.sibling(index.row(), 0);
            const quint64 runtime = roleAddress(nameIndex);
            const QString name = nameIndex.data().toString();
            QMenu menu(this);
            menu.addAction(tr("Seek static address"), this, [this, runtime]() {
                fridaSeekRuntime(session, runtime);
            });
            menu.addAction(tr("Copy runtime address"), this, [runtime]() {
                QApplication::clipboard()->setText(RAddressString(runtime));
            });
            menu.addAction(tr("Live hexdump"), this, [this, runtime]() {
                fridaShowLiveHex(this, session, runtime);
            });
            menu.addAction(tr("Show exports"), this, [this, name]() {
                fridaShowExports(this, session, name);
            });
            menu.addAction(tr("Import symbols into analysis"), this, [this, name]() {
                session->importExports(name);
            });
            menu.exec(view->viewport()->mapToGlobal(pos));
        });
        refill();
    }

private:
    void refill()
    {
        model->removeRows(0, model->rowCount());
        for (const FridaModuleInfo &module : session->modules()) {
            auto *name = textItem(module.name);
            name->setData(QVariant::fromValue(module.base), Qt::UserRole);
            name->setToolTip(session->describe(module.base));
            model->appendRow(
                {name,
                 addressItem(session->formatRuntime(module.base), module.base),
                 textItem(formatSize(module.size)),
                 textItem(module.path)});
        }
    }

    FridaSession *session = nullptr;
    QStandardItemModel *model = nullptr;
    QSortFilterProxyModel *proxy = nullptr;
    QTreeView *view = nullptr;
};

class FridaMapsDock : public QWidget
{
public:
    FridaMapsDock(FridaSession *fridaSession)
        : QWidget(nullptr)
        , session(fridaSession)
    {
        setObjectName(QStringLiteral("FridaMemoryMapDock"));
        setWindowTitle(tr("Memory Map"));
        model = new QStandardItemModel(this);
        model->setHorizontalHeaderLabels({tr("Start"), tr("End"), tr("Perm"), tr("Module")});
        proxy = new QSortFilterProxyModel(this);
        proxy->setSourceModel(model);
        proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        proxy->setFilterKeyColumn(-1);

        auto *root = new QWidget(this);
        auto *layout = new QVBoxLayout(root);
        auto *filter = new QLineEdit(root);
        filter->setPlaceholderText(tr("Filter"));
        view = new QTreeView(root);
        view->setModel(proxy);
        view->setRootIsDecorated(false);
        view->setAlternatingRowColors(true);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        view->header()->setStretchLastSection(true);
        layout->addWidget(filter);
        layout->addWidget(view);
        host(this, root);

        connect(
            filter, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
        connect(session, &FridaSession::changed, this, [this]() { refill(); });
        connect(view, &QTreeView::activated, this, [this](const QModelIndex &index) {
            fridaSeekRuntime(session, roleAddress(index.sibling(index.row(), 0)));
        });
        connect(view, &QTreeView::customContextMenuRequested, this, [this](const QPoint &pos) {
            const QModelIndex index = view->indexAt(pos);
            if (!index.isValid()) {
                return;
            }
            const quint64 runtime = roleAddress(index.sibling(index.row(), 0));
            QMenu menu(this);
            menu.addAction(tr("Seek static address"), this, [this, runtime]() {
                fridaSeekRuntime(session, runtime);
            });
            menu.addAction(tr("Live hexdump"), this, [this, runtime]() {
                fridaShowLiveHex(this, session, runtime);
            });
            menu.addAction(tr("Copy address"), this, [runtime]() {
                QApplication::clipboard()->setText(RAddressString(runtime));
            });
            menu.exec(view->viewport()->mapToGlobal(pos));
        });
        refill();
    }

private:
    void refill()
    {
        model->removeRows(0, model->rowCount());
        for (const FridaMapInfo &map : session->maps()) {
            const QString module = QFileInfo(map.path).fileName();
            auto *start = addressItem(session->formatRuntime(map.start), map.start);
            start->setToolTip(session->describe(map.start));
            auto *name = textItem(module.isEmpty() ? map.path : module);
            name->setToolTip(map.path);
            model->appendRow(
                {start,
                 addressItem(session->formatRuntime(map.end), map.end),
                 textItem(map.permission),
                 name});
        }
    }

    FridaSession *session = nullptr;
    QStandardItemModel *model = nullptr;
    QSortFilterProxyModel *proxy = nullptr;
    QTreeView *view = nullptr;
};

class FridaHooksDock : public QWidget
{
public:
    FridaHooksDock(FridaSession *fridaSession)
        : QWidget(nullptr)
        , session(fridaSession)
    {
        setObjectName(QStringLiteral("FridaHooksDock"));
        setWindowTitle(tr("Hooks"));
        model = new QStandardItemModel(this);
        model->setHorizontalHeaderLabels({tr("Address"), tr("Summary")});
        view = new QTreeView(this);
        view->setModel(model);
        view->setRootIsDecorated(false);
        view->setAlternatingRowColors(true);
        view->header()->setStretchLastSection(true);
        host(this, view);
        connect(session, &FridaSession::changed, this, [this]() { refill(); });
        connect(view, &QTreeView::activated, this, [this](const QModelIndex &index) {
            fridaSeekRuntime(session, roleAddress(index.sibling(index.row(), 0)));
        });
        refill();
    }

private:
    void refill()
    {
        model->removeRows(0, model->rowCount());
        for (const FridaHookInfo &hook : session->hooks()) {
            model->appendRow(
                {addressItem(session->formatRuntime(hook.address), hook.address),
                 textItem(hook.summary)});
        }
    }

    FridaSession *session = nullptr;
    QStandardItemModel *model = nullptr;
    QTreeView *view = nullptr;
};

class FridaTracesDock : public QWidget
{
public:
    FridaTracesDock(FridaSession *fridaSession)
        : QWidget(nullptr)
        , session(fridaSession)
    {
        setObjectName(QStringLiteral("FridaTracesDock"));
        setWindowTitle(tr("Traces"));
        model = new QStandardItemModel(this);
        model->setHorizontalHeaderLabels({tr("Function"), tr("Hits")});
        view = new QTreeView(this);
        view->setModel(model);
        view->setRootIsDecorated(false);
        view->setAlternatingRowColors(true);
        view->header()->setStretchLastSection(true);
        view->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        auto *root = new QWidget(this);
        auto *layout = new QVBoxLayout(root);
        auto *refresh = new QPushButton(tr("Refresh"), root);
        layout->addWidget(refresh);
        layout->addWidget(view);
        host(this, root);
        connect(refresh, &QPushButton::clicked, this, [this]() { reload(); });
        connect(session, &FridaSession::changed, this, [this]() {
            if (!session->isAttached()) {
                model->removeRows(0, model->rowCount());
            }
        });
        connect(view, &QTreeView::activated, this, [this](const QModelIndex &index) {
            const QString line = index.sibling(index.row(), 0).data().toString();
            static const QRegularExpression addressRe(QStringLiteral("0x[0-9a-fA-F]+"));
            const QRegularExpressionMatch match = addressRe.match(line);
            if (!match.hasMatch()) {
                return;
            }
            bool ok = false;
            const quint64 addr = match.captured(0).toULongLong(&ok, 16);
            if (ok) {
                fridaSeekRuntime(session, addr);
            }
        });
    }

private:
    void reload()
    {
        if (!session->isAttached()) {
            model->removeRows(0, model->rowCount());
            return;
        }
        session->command(QStringLiteral("dt"), [this](const QString &output) {
            model->removeRows(0, model->rowCount());
            for (const QString &raw : output.split(QLatin1Char('\n'))) {
                const QString line = raw.trimmed();
                if (line.isEmpty()) {
                    continue;
                }
                QString function = line;
                QString hits;
                const int space = line.lastIndexOf(QLatin1Char(' '));
                if (space > 0) {
                    bool ok = false;
                    line.mid(space + 1).toULongLong(&ok);
                    if (ok) {
                        function = line.left(space).trimmed();
                        hits = line.mid(space + 1);
                    }
                }
                model->appendRow({textItem(function), textItem(hits)});
            }
        });
    }

    FridaSession *session = nullptr;
    QStandardItemModel *model = nullptr;
    QTreeView *view = nullptr;
};

class FridaRuntimePage : public QWidget
{
public:
    explicit FridaRuntimePage(FridaSession *fridaSession)
        : QWidget(nullptr)
        , session(fridaSession)
    {
        setWindowTitle(tr("ObjC/Java Runtime"));
        auto *layout = new QVBoxLayout(this);
        auto *row = new QHBoxLayout();
        kind = new QComboBox(this);
        kind->addItem(tr("Objective-C"));
        kind->addItem(tr("Java"));
        auto *refresh = new QPushButton(tr("Refresh"), this);
        view = new QPlainTextEdit(this);
        view->setReadOnly(true);
        row->addWidget(kind);
        row->addWidget(refresh);
        row->addStretch();
        layout->addLayout(row);
        layout->addWidget(view);
        connect(refresh, &QPushButton::clicked, this, [this]() { reload(); });
        connect(session, &FridaSession::changed, this, [this]() {
            if (!session->isAttached()) {
                view->clear();
            }
        });
    }

private:
    void reload()
    {
        if (kind->currentIndex() == 0) {
            showCommand(session, QStringLiteral("ic"), view);
        } else {
            loadJava(session, view);
        }
    }

    FridaSession *session = nullptr;
    QComboBox *kind = nullptr;
    QPlainTextEdit *view = nullptr;
};

} // namespace

QList<QWidget *> createFridaPanels(FridaSession *session)
{
    const auto command = [session](const QString &name) {
        return [session, name](QPlainTextEdit *view) { showCommand(session, name, view); };
    };
    QList<QWidget *> panels;
    panels << new FridaModulesDock(session);
    panels << new FridaOutputDock(
        QObject::tr("Threads"),
        QStringLiteral("FridaThreadsPage"),
        session,
        command(QStringLiteral("dpt")));
    panels << new FridaMapsDock(session);
    panels << new FridaOutputDock(
        QObject::tr("Breakpoints"),
        QStringLiteral("FridaBreakpointsPage"),
        session,
        command(QStringLiteral("db")));
    panels << new FridaRuntimePage(session);
    panels << new FridaHooksDock(session);
    panels << new FridaTracesDock(session);
    return panels;
}
