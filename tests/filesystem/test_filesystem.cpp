#include "common/SourceLineReference.h"
#include "core/Iaito.h"
#include "widgets/FilesystemWidget.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class FilesystemTest : public QObject
{
    Q_OBJECT
    QTemporaryDir temporary;

    void addReference(quint64 address, const QString &file, int line)
    {
        const QByteArray encoded
            = (file + QLatin1Char(':') + QString::number(line)).toUtf8().toBase64();
        Core()->cmd(QStringLiteral("CL %1 base64:%2")
                        .arg(RAddressString(address), QString::fromLatin1(encoded)));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(temporary.isValid());
        Core()->initialize(false);
        QFile binary(temporary.filePath("binary"));
        QVERIFY(binary.open(QIODevice::WriteOnly));
        QCOMPARE(binary.write(QByteArray(1024, '\0')), qint64(1024));
        binary.close();
        Core()->cmd(QStringLiteral("o %1").arg(binary.fileName()));
        Core()->cmd(
            "af+ 0x100 first;afb+ 0x100 0x100 0x20;"
            "af+ 0x200 second;afb+ 0x200 0x200 0x10");
        qRegisterMetaType<RVA>("RVA");
    }

    void sourceRows()
    {
        const auto rows = SourceLineReference::parse(
            "0x00000000\t0\t\n0xffff800000000001\t12\t\treturn a < b;\r\n"
            "0x100\t3\tmissing source text is allowed\n"
            "bogus\t1\tbad\n0xno\t2\tbad\n0x100\t-2\tbad\n0x100\tbad\tbad\n");
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].address, quint64(0));
        QCOMPARE(rows[0].line, 0);
        QCOMPARE(rows[1].address, Q_UINT64_C(0xffff800000000001));
        QCOMPARE(rows[1].code, QString("\treturn a < b;"));
        QVERIFY(SourceLineReference::parse("plain file contents").isEmpty());
    }

    void browseAndNavigate()
    {
        // The source file intentionally does not exist on this machine.
        const QString file = QStringLiteral("/project with spaces/src/main;@.c");
        addReference(0x100, file, 3);
        addReference(0x110, file, 4);
        addReference(0x200, file, 8);
        addReference(0x300, file, 10);
        addReference(0x400, QStringLiteral("/project with spaces/lib/helper.c"), 2);
        Core()->cmd("m /custom r2");

        FilesystemWidget widget(nullptr);
        widget.resize(900, 800);
        widget.show();
        widget.showSourceFiles();
        auto *tree = widget.findChild<QTreeView *>("filesystemTree");
        auto *path = widget.findChild<QLineEdit *>("filesystemPath");
        auto *functions = widget.findChild<QTreeWidget *>("sourceFunctions");
        auto *lines = widget.findChild<QTreeWidget *>("sourceLines");
        QVERIFY(tree && path && functions && lines);
        QCOMPARE(path->text(), QStringLiteral("/custom/cl"));
        const int mountCount = Core()->cmdj("mj").object()["mountpoints"].toArray().size();
        widget.showSourceFiles();
        QCOMPARE(Core()->cmdj("mj").object()["mountpoints"].toArray().size(), mountCount);

        auto *model = qobject_cast<FilesystemTreeModel *>(tree->model());
        QVERIFY(model);
        QCOMPARE(model->rowCount(), 1);
        const QModelIndex project = model->index(0, 0);
        QCOMPARE(project.data().toString(), QStringLiteral("project with spaces"));
        tree->expand(project);
        QCOMPARE(model->rowCount(project), 2);
        QModelIndex source;
        for (int row = 0; row < model->rowCount(project); ++row) {
            const auto candidate = model->index(row, 0, project);
            if (candidate.data().toString() == QStringLiteral("src")) {
                source = candidate;
            }
        }
        QVERIFY(source.isValid());
        tree->expand(source);
        const auto sourceFile = model->index(0, 0, source);
        QCOMPARE(sourceFile.data().toString(), QStringLiteral("main;@.c"));
        QCOMPARE(
            sourceFile.data(Qt::UserRole).toString(),
            QStringLiteral("/custom/cl/project with spaces/src/main;@.c"));
        // Selection on a secondary column must still preview the correct file.
        tree->setCurrentIndex(sourceFile.sibling(sourceFile.row(), 1));
        QCOMPARE(functions->topLevelItemCount(), 2);
        QCOMPARE(lines->topLevelItemCount(), 4);
        QCOMPARE(functions->topLevelItem(0)->text(0), QStringLiteral("first"));
        QCOMPARE(functions->topLevelItem(0)->data(0, Qt::UserRole).toULongLong(), quint64(0x100));
        QVERIFY(lines->topLevelItem(0)->text(2).isEmpty());
        QApplication::processEvents();
        const QString previewImage = qEnvironmentVariable("IAITO_FILESYSTEM_PREVIEW");
        if (!previewImage.isEmpty()) {
            QVERIFY(widget.grab().save(previewImage));
        }

        QSignalSpy seeks(Core(), &IaitoCore::seekChanged);
        QSignalSpy showCode(Core(), &IaitoCore::showMemoryWidgetRequested);
        QTest::mouseClick(
            functions->viewport(),
            Qt::LeftButton,
            Qt::NoModifier,
            functions->visualItemRect(functions->topLevelItem(1)).center());
        QCOMPARE(Core()->getOffset(), RVA(0x200));
        QCOMPARE(seeks.size(), 1);
        // Unanalyzed addresses remain navigable through the source-line preview.
        widget.findChild<QTabWidget *>()->setCurrentWidget(lines);
        QApplication::processEvents();
        lines->setCurrentItem(lines->topLevelItem(3));
        lines->setFocus();
        QVERIFY(lines->currentItem());
        QCOMPARE(lines->currentItem()->data(0, Qt::UserRole).toULongLong(), quint64(0x300));
        QSignalSpy activated(lines, &QTreeWidget::itemActivated);
#ifdef Q_OS_MACOS
        QTest::keyClick(lines, Qt::Key_O, Qt::ControlModifier);
#else
        QTest::keyClick(lines, Qt::Key_Return);
#endif
        QCOMPARE(activated.size(), 1);
        QCOMPARE(Core()->getOffset(), RVA(0x300));
        QCOMPARE(seeks.size(), 2);
        QCOMPARE(showCode.size(), 2);
        Core()->cmd("afn renamed 0x100");
        Core()->functionsChanged();
        QCOMPARE(functions->topLevelItem(0)->text(0), QStringLiteral("renamed"));
        QCOMPARE(
            tree->currentIndex().data(Qt::UserRole).toString(),
            QStringLiteral("/custom/cl/project with spaces/src/main;@.c"));
        QVERIFY(tree->isExpanded(tree->currentIndex().parent()));
        QCOMPARE(tree->header()->sectionResizeMode(0), QHeaderView::Stretch);

        path->setText(QStringLiteral("/"));
        path->returnPressed();
        bool foundMount = false;
        for (int row = 0; row < model->rowCount(); ++row) {
            foundMount |= model->index(row, 0).data(Qt::UserRole).toString() == "/custom";
        }
        QVERIFY(foundMount);
        QCOMPARE(lines->topLevelItemCount(), 0);
    }

    void filePreviews()
    {
        QFile source(temporary.filePath("available source.c"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("\treturn a < b;\n"), qint64(15));
        source.close();
        addReference(0x100, source.fileName(), 1);
        FilesystemWidget widget(nullptr);
        widget.showSourceFiles();
        auto *path = widget.findChild<QLineEdit *>("filesystemPath");
        path->setText(QStringLiteral("/custom/cl") + temporary.path());
        path->returnPressed();
        auto *tree = widget.findChild<QTreeView *>("filesystemTree");
        tree->setCurrentIndex(tree->model()->index(0, 0));
        auto *lines = widget.findChild<QTreeWidget *>("sourceLines");
        QCOMPARE(lines->topLevelItemCount(), 1);
        QCOMPARE(lines->topLevelItem(0)->text(2), QStringLiteral("\treturn a < b;"));

        path->setText(QStringLiteral("/custom"));
        path->returnPressed();
        QModelIndex version;
        for (int row = 0; row < tree->model()->rowCount(); ++row) {
            const auto candidate = tree->model()->index(row, 0);
            if (candidate.data().toString() == QStringLiteral("version")) {
                version = candidate;
            }
        }
        QVERIFY(version.isValid());
        tree->setCurrentIndex(version);
        const auto *contents = widget.findChild<QPlainTextEdit *>("filesystemContents");
        QVERIFY(contents);
        QCOMPARE(contents->toPlainText().trimmed(), Core()->cmd("?V").trimmed());
        QCOMPARE(lines->topLevelItemCount(), 0);
    }

    void automaticMount()
    {
        Core()->cmd("m-/custom;m /r2 tmp");
        FilesystemWidget widget(nullptr);
        widget.showSourceFiles();
        QCOMPARE(widget.findChild<QLineEdit *>("filesystemPath")->text(), QString("/r2_1/cl"));
    }
};

QTEST_MAIN(FilesystemTest)
#include "test_filesystem.moc"
