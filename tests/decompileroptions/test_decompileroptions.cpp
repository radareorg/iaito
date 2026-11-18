#include "common/Configuration.h"
#include "common/R2DecaiDecompiler.h"
#include "common/R2pdcCmdDecompiler.h"
#include "core/MainWindow.h"
#include "widgets/DecompilerWidget.h"

#include <QComboBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class DecompilerOptionTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

    static QString runJs(const QByteArray &script)
    {
        return Core()->cmd(("js base64:" + script.toBase64()).constData());
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("decompileroptions");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setDecompilerAutoRefreshEnabled(false);
        QVERIFY(Core()->registerDecompiler(new R2pdcCmdDecompiler));
        QVERIFY(Core()->registerDecompiler(new R2DecaiDecompiler));

        // Mimic decai's r2js output without registering native eval variables.
        runJs(R"(
            var decaiTestLog = console.log;
            var decaiTestOptions = {
                model: "test model", lang: "C", prompt: "", headers: "X-Test=a=b"
            };
            r2.plugin("core", function() {
                return {name: "iaito-decai-options", call: function(command) {
                    if (command === "decai -e") {
                        Object.keys(decaiTestOptions).forEach(function(key) {
                            console.log("decai -e " + key + "=" + decaiTestOptions[key]);
                        });
                        return true;
                    }
                    if (command.startsWith("decai -e ")) {
                        var assignment = command.slice(9);
                        var equal = assignment.indexOf("=");
                        decaiTestOptions[assignment.slice(0, equal)] = assignment.slice(equal + 1);
                        return true;
                    }
                    return false;
                }};
            });
        )");
        QCOMPARE(runJs("r2.log(console.log === decaiTestLog);"), QString("true\n"));
        QVERIFY(Core()->cmd("e decai.").isEmpty());
    }

    void listAndEditOptions()
    {
        Decompiler *decai = Core()->getDecompilerById("decai");
        QCOMPARE(decai->listOptions(),
                 QStringList({"model=test model", "lang=C", "prompt=", "headers=X-Test=a=b"}));
        QCOMPARE(decai->getOption("model"), QString("test model"));
        QCOMPARE(decai->getOption("headers"), QString("X-Test=a=b"));
        QVERIFY(decai->getOption("prompt").isEmpty());
        decai->setOption("lang", "Rust");
        QCOMPARE(decai->getOption("lang"), QString("Rust"));
        decai->setOption("lang", "C");
        QCOMPARE(runJs("r2.log(console.log === decaiTestLog);"), QString("true\n"));
    }

    void populateOptionsOnSelection()
    {
        Config()->setSelectedDecompiler("pdc");
        MainWindow main;
        DecompilerWidget widget(&main);
        auto *decompilers = widget.findChild<QComboBox *>("decompilerComboBox");
        auto *options = widget.findChild<QComboBox *>("optionsComboBox");
        QVERIFY(decompilers);
        QVERIFY(options);
        QVERIFY(options->isHidden());
        const int decaiIndex = decompilers->findData("decai");
        QVERIFY(decaiIndex >= 0);
        decompilers->setCurrentIndex(decaiIndex);
        QVERIFY(!options->isHidden());
        QVERIFY(options->isEnabled());
        QCOMPARE(options->count(), 4);
        QCOMPARE(options->itemText(0), QString("decai.model=test model"));
        QCOMPARE(options->itemData(0).toString(), QString("model"));
        QCOMPARE(options->itemText(2), QString("decai.prompt="));
        QCOMPARE(options->itemText(3), QString("decai.headers=X-Test=a=b"));
    }
};

QTEST_MAIN(DecompilerOptionTests)
#include "test_decompileroptions.moc"
