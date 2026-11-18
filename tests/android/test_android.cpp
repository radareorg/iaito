#include "common/Android.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class AndroidRuntimeTest : public QObject
{
    Q_OBJECT

private slots:
    void installsAndUpdatesAssets()
    {
#if !defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
        QSKIP("This host test redirects Qt's standard paths with XDG environment variables.");
#endif
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString root = temporary.path();
        const QString assets = root + "/assets/radare2";
        QVERIFY(QDir().mkpath(assets + "/share/radare2/test"));
        QDir::setSearchPaths("assets", {root + "/assets"});

        auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        auto resetPaths = [&]() {
            qputenv("XDG_DATA_HOME", (root + "/data").toUtf8());
            qputenv("XDG_CONFIG_HOME", (root + "/config").toUtf8());
            qputenv("XDG_CACHE_HOME", (root + "/cache").toUtf8());
        };
        resetPaths();
        const QString prefix = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                               + "/radare2";
        const QString relative = "share/radare2/test/types.sdb";
        QVERIFY(write(assets + "/files.txt", relative.toUtf8() + '\n'));
        QVERIFY(write(assets + "/bundle-id", "first"));
        QVERIFY(write(assets + '/' + relative, QByteArray("database\0bytes", 14)));

        QString error;
        QVERIFY2(Iaito::initializeAndroid(&error), qPrintable(error));
        QCOMPARE(read(prefix + '/' + relative), QByteArray("database\0bytes", 14));
        QCOMPARE(qgetenv("R2_PREFIX"), prefix.toUtf8());
        QCOMPARE(read(prefix + "/bundle-id"), QByteArray("first"));

        // A completed installation is reused on the next launch.
        QVERIFY(QFile::remove(assets + '/' + relative));
        resetPaths();
        QVERIFY2(Iaito::initializeAndroid(&error), qPrintable(error));

        // An interrupted update must not be marked complete; a later launch retries.
        QVERIFY(write(assets + "/bundle-id", "second"));
        resetPaths();
        QVERIFY(!Iaito::initializeAndroid(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(read(prefix + "/bundle-id"), QByteArray("first"));
        QVERIFY(write(assets + '/' + relative, "new database"));
        resetPaths();
        QVERIFY2(Iaito::initializeAndroid(&error), qPrintable(error));
        QCOMPARE(read(prefix + '/' + relative), QByteArray("new database"));
        QCOMPARE(read(prefix + "/bundle-id"), QByteArray("second"));
    }
};

QTEST_GUILESS_MAIN(AndroidRuntimeTest)
#include "test_android.moc"
