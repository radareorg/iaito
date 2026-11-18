#include "dialogs/PackageManagerDialog.h"

#include <cstdio>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

static int fakeR2pm(const QStringList &args)
{
    QFile log(qEnvironmentVariable("IAITO_TEST_R2PM_LOG"));
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return 1;
    }
    log.write(args.join(' ').toUtf8() + '\n');
    if (args.first() == "-sj" || args.first() == "-Asj") {
        const auto packages = QJsonDocument::fromJson(R"([
            {"name":"source","desc":"shared","platforms":["linux","darwin"],"binary":false},
            {"name":"script","desc":"Portable script","platforms":["r2js"],"binary":true},
            {"name":"foreign","desc":"Other platform","platforms":["linux-x86-32"],
             "binary":true,"supported":false},
            {"name":"both","desc":"shared","platforms":["linux","darwin","termux"],
             "binary":true},
            {"name":"legacy","desc":"No binary metadata","platforms":["windows","unix"]}
        ])")
                                  .array();
        QJsonArray result;
        for (const auto &package : packages) {
            if (args.first() == "-Asj" || qEnvironmentVariable("R2PM_PLATFORM") == "any"
                || package.toObject()["supported"].toBool(true)) {
                result.append(package);
            }
        }
        std::puts(QJsonDocument(result).toJson().constData());
    } else if (args.first() == "-l") {
        std::puts("source\nboth");
    }
    return 0;
}

class PackageManagerTests : public QObject
{
    Q_OBJECT

    QTemporaryDir directory;
    QByteArray originalPath;

    static QCheckBox *checkBox(PackageManagerDialog &dialog, const QString &text)
    {
        for (auto *box : dialog.findChildren<QCheckBox *>()) {
            if (box->text() == text) {
                return box;
            }
        }
        return nullptr;
    }

    static QStringList visiblePackages(QTableWidget *table)
    {
        QStringList result;
        for (int row = 0; row < table->rowCount(); ++row) {
            if (!table->isRowHidden(row)) {
                result << table->item(row, 1)->text();
            }
        }
        result.sort();
        return result;
    }

    QByteArray commands() const
    {
        QFile log(directory.filePath("commands"));
        if (!log.open(QIODevice::ReadOnly)) {
            return {};
        }
        return log.readAll();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(directory.isValid());
#ifdef Q_OS_WIN
        const QString executable = directory.filePath("r2pm.exe");
#else
        const QString executable = directory.filePath("r2pm");
#endif
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), executable));
        originalPath = qgetenv("PATH");
        qputenv(
            "PATH",
            directory.path().toLocal8Bit() + QDir::listSeparator().toLatin1() + originalPath);
        qputenv("IAITO_TEST_R2PM_LOG", directory.filePath("commands").toLocal8Bit());
    }

    void cleanupTestCase()
    {
        qputenv("PATH", originalPath);
        qunsetenv("IAITO_TEST_R2PM_LOG");
    }

    void filtersAndSelection()
    {
        PackageManagerDialog dialog;
        auto *table = dialog.findChild<QTableWidget *>();
        auto *filter = dialog.findChild<QLineEdit *>();
        auto *binary = checkBox(dialog, "Binary packages");
        auto *all = checkBox(dialog, "Show all platforms");
        QVERIFY(table && filter && binary && all);
        QVERIFY(!binary->isChecked());
        QCOMPARE(visiblePackages(table), QStringList({"both", "legacy", "script", "source"}));
        table->setCurrentItem(table->findItems("source", Qt::MatchExactly).first());
        const QByteArray beforeToggle = commands();
        binary->setChecked(true);
        QCOMPARE(commands(), beforeToggle);
        const QStringList nativePackages = {"both", "script"};
        QCOMPARE(visiblePackages(table), nativePackages);
        QCOMPARE(table->currentRow(), -1);
        QVERIFY(table->selectedItems().isEmpty());
        filter->setText("R2JS");
        QCOMPARE(visiblePackages(table), QStringList({"script"}));
        filter->setText("DARWIN");
        QCOMPARE(visiblePackages(table), QStringList({"both"}));
        filter->setText("termux");
        QCOMPARE(visiblePackages(table), QStringList({"both"}));
        filter->setText("SHARED");
        QCOMPARE(visiblePackages(table), QStringList({"both"}));
        table->setCurrentItem(table->findItems("both", Qt::MatchExactly).first());
        all->setChecked(true);
        QCOMPARE(visiblePackages(table), QStringList({"both"}));
        QCOMPARE(table->currentItem()->text(), QString("both"));
        filter->clear();
        QCOMPARE(visiblePackages(table), QStringList({"both", "foreign", "script"}));
        filter->setText("linux-x86-32");
        QCOMPARE(visiblePackages(table), QStringList({"foreign"}));
        const int foreignRow = table->findItems("foreign", Qt::MatchExactly).first()->row();
        QCOMPARE(table->item(foreignRow, 2)->text(), QString("linux-x86-32"));
        filter->clear();
        table->sortItems(1, Qt::DescendingOrder);
        binary->setChecked(false);
        binary->setChecked(true);
        QCOMPARE(visiblePackages(table), QStringList({"both", "foreign", "script"}));
        table->setCurrentItem(table->findItems("foreign", Qt::MatchExactly).first());
        all->setChecked(false);
        QCOMPARE(visiblePackages(table), nativePackages);
        QCOMPARE(table->currentRow(), -1);
        QVERIFY(table->selectedItems().isEmpty());
        QCOMPARE(commands(), beforeToggle);
        const int installedRow = table->findItems("both", Qt::MatchExactly).first()->row();
        QCOMPARE(table->item(installedRow, 0)->text(), QString("1"));
        QCOMPARE(table->item(installedRow, 2)->text(), QString("linux darwin termux"));
        QVERIFY(QMetaObject::invokeMethod(&dialog, "refreshPackages"));
        QCOMPARE(commands().mid(beforeToggle.size()), QByteArray("-l\n-U\n-Asj\n"));
        QCOMPARE(visiblePackages(table), nativePackages);
        const QByteArray afterRefresh = commands();
        all->setChecked(true);
        QCOMPARE(visiblePackages(table), QStringList({"both", "foreign", "script"}));
        QCOMPARE(commands(), afterRefresh);
    }

    void installAndUninstall_data()
    {
        QTest::addColumn<bool>("binaryMode");
        QTest::addColumn<QByteArray>("installCommand");
        QTest::newRow("source") << false << QByteArray("-ci both\n");
        QTest::newRow("binary") << true << QByteArray("-bi both\n");
    }

    void installAndUninstall()
    {
        QFETCH(bool, binaryMode);
        QFETCH(QByteArray, installCommand);
        PackageManagerDialog dialog;
        auto *table = dialog.findChild<QTableWidget *>();
        auto *binary = checkBox(dialog, "Binary packages");
        auto *process = dialog.findChild<QProcess *>();
        QVERIFY(table && binary && process);
        binary->setChecked(binaryMode);
        QSignalSpy finished(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished));
        const auto beforeInstall = commands().size();
        table->setCurrentItem(table->findItems("both", Qt::MatchExactly).first());
        QVERIFY(QMetaObject::invokeMethod(&dialog, "installPackage"));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(commands().mid(beforeInstall).startsWith(installCommand));
        const auto beforeUninstall = commands().size();
        table->setCurrentItem(table->findItems("both", Qt::MatchExactly).first());
        QVERIFY(QMetaObject::invokeMethod(&dialog, "uninstallPackage"));
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY(commands().mid(beforeUninstall).startsWith("-u both\n"));
    }
};

int main(int argc, char **argv)
{
    if (QFileInfo(QString::fromLocal8Bit(argv[0])).completeBaseName() == "r2pm") {
        QCoreApplication app(argc, argv);
        return fakeR2pm(app.arguments().mid(1));
    }
    QApplication app(argc, argv);
    PackageManagerTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_package_manager.moc"
