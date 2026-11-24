#include "common/SourceLineReference.h"
#include "core/FilesystemEvidence.h"
#include "core/Iaito.h"
#include "widgets/FilesystemWidget.h"
#include "widgets/ForensicsWidget.h"

#include <algorithm>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QStyleOptionComboBox>
#include <QTableView>
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

    void readOnlyEvidence()
    {
        constexpr int sector = 512;
        constexpr int sectors = 36;
        QByteArray fat(sector * sectors, '\0');
        auto put = [&fat](int offset, quint32 value, int length) {
            for (int i = 0; i < length; ++i) {
                fat[offset + i] = char((value >> (8 * i)) & 0xff);
            }
        };
        fat.replace(0, 3, QByteArray::fromHex("eb3c90"));
        fat.replace(3, 8, "MSDOS5.0");
        put(11, sector, 2);
        put(13, 1, 1);
        put(14, 1, 2);
        put(16, 1, 1);
        put(17, 16, 2);
        put(19, sectors, 2);
        put(21, 0xf8, 1);
        put(22, 1, 2);
        put(24, 18, 2);
        put(26, 2, 2);
        fat.replace(54, 8, "FAT12   ");
        put(510, 0xaa55, 2);
        fat.replace(sector, 6, QByteArray::fromHex("f8ffffffffff"));
        fat.replace(2 * sector, 11, "FIRST   TXT");
        put(2 * sector + 11, 0x20, 1);
        put(2 * sector + 26, 2, 2);
        put(2 * sector + 28, 2, 4);
        fat.replace(2 * sector + 32, 11, QByteArray("\xe5" "ELETED TXT", 11));
        put(2 * sector + 43, 0x20, 1);
        put(2 * sector + 58, 3, 2);
        put(2 * sector + 60, 1, 4);
        fat.replace(3 * sector, 2, "AB");
        fat[4 * sector] = 'D';
        QImage image(1, 1, QImage::Format_RGB32);
        image.fill(Qt::green);
        QBuffer png;
        QVERIFY(png.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&png, "PNG"));
        QVERIFY(png.data().size() < sector - 32);
        fat.replace(4 * sector + 32, png.data().size(), png.data());
        fat.replace(5 * sector - 1, 5, "CFREE");

        QFile first(temporary.filePath("first image.img"));
        QVERIFY(first.open(QIODevice::WriteOnly));
        QCOMPARE(first.write(fat), qint64(fat.size()));
        first.close();
        QFile second(temporary.filePath("second image.img"));
        QVERIFY(second.open(QIODevice::WriteOnly));
        QByteArray mbr(sector * 8, '\0');
        mbr[447] = 1;
        mbr[450] = 1;
        mbr[451] = 1;
        mbr[454] = 8;
        mbr[458] = sectors;
        mbr[510] = '\x55';
        mbr[511] = '\xaa';
        fat.replace(2 * sector, 11, "SECOND  TXT");
        fat.replace(3 * sector, 2, "XY");
        QCOMPARE(second.write(mbr + fat), qint64(mbr.size() + fat.size()));
        second.close();

        QString error;
        const auto raw = Core()->openFilesystemEvidence(first.fileName(), &error);
        QVERIFY2(raw, qPrintable(error));
        QCOMPARE(raw->volumes().size(), 1);
        QCOMPARE(raw->volumes().first().filesystem, QStringLiteral("fat"));
        const auto partitioned = Core()->openFilesystemEvidence(second.fileName(), &error);
        QVERIFY2(partitioned, qPrintable(error));
        QCOMPARE(partitioned->volumes().size(), 1);
        QCOMPARE(partitioned->volumes().first().start, quint64(mbr.size()));
        QCOMPARE(partitioned->volumes().first().length, quint64(fat.size()));
#if R2_ABIVERSION >= 188
        QFile shortPartition(temporary.filePath("short partition.img"));
        QVERIFY(shortPartition.open(QIODevice::WriteOnly));
        auto shortMbr = mbr;
        shortMbr[458] = 4;
        QCOMPARE(shortPartition.write(shortMbr + fat), qint64(shortMbr.size() + fat.size()));
        shortPartition.close();
        const auto truncated = Core()->openFilesystemEvidence(shortPartition.fileName(), &error);
        QVERIFY2(truncated, qPrintable(error));
        QCOMPARE(truncated->volumes().size(), 1);
        QCOMPARE(truncated->volumes().first().length, quint64(4 * sector));
        QVERIFY(truncated->volumes().first().mount.isEmpty());
#endif
        bool readable;
        const auto secondEntries = partitioned->listDirectory(0, {}, &readable);
        QVERIFY(readable);
        QCOMPARE(secondEntries.size(), 1);
        QCOMPARE(secondEntries.first().name.toLower(), QStringLiteral("second.txt"));
        const auto firstEntries = raw->listDirectory(0, {}, &readable);
        QVERIFY(readable);
        QCOMPARE(firstEntries.size(), 1);
        QCOMPARE(firstEntries.first().name.toLower(), QStringLiteral("first.txt"));
        QCOMPARE(firstEntries.first().size, quint64(2));
        const auto firstPage = raw->readFile(0, firstEntries.first(), 1, 4096);
        QVERIFY(firstPage.opened);
        QVERIFY(!firstPage.error);
        QCOMPARE(firstPage.size, quint64(2));
        QCOMPARE(firstPage.bytes, QByteArray("B"));
        const auto secondPage = partitioned->readFile(0, secondEntries.first(), 0, 1);
        QVERIFY(secondPage.opened);
        QVERIFY(!secondPage.error);
        QCOMPARE(secondPage.bytes, QByteArray("X"));
        QCOMPARE(partitioned->readImage(mbr.size() + 3 * sector, 2).bytes, QByteArray("XY"));
        const QByteArray imageDigest
            = QCryptographicHash::hash(mbr + fat, QCryptographicHash::Sha256).toHex();
        QCOMPARE(partitioned->imageSha256(), imageDigest);
#if R2_ABIVERSION >= 187
        QVERIFY(partitioned->volumes().first().deletedEntries);
        using View = FilesystemEvidence::DirectoryView;
        const auto deleted = partitioned->listDirectory(0, {}, &readable, View::Deleted);
        QVERIFY(readable);
        QCOMPARE(deleted.size(), 1);
        QVERIFY(deleted.first().deleted);
        QCOMPARE(deleted.first().id, quint64(2 * sector + 32));
        QCOMPARE(deleted.first().firstOffset, quint64(mbr.size() + 4 * sector));
#if R2_ABIVERSION >= 188
        const auto deletedPage = partitioned->readFile(0, deleted.first(), 0, 4096);
        QVERIFY(deletedPage.opened);
        QVERIFY(!deletedPage.error);
        QCOMPARE(deletedPage.bytes, QByteArray("D"));
        using Evidence = FilesystemEvidence;
        const auto extents = partitioned->fileMapping(
            0, secondEntries.first(), Evidence::MappingMethod::Metadata);
        QVERIFY(extents.status == Evidence::WalkStatus::Complete);
        QCOMPARE(extents.ranges.size(), 1);
        QCOMPARE(extents.ranges.first().logical, quint64(0));
        QCOMPARE(extents.ranges.first().physical, quint64(mbr.size() + 3 * sector));
        QCOMPARE(extents.ranges.first().length, quint64(2));
        const auto owners = partitioned->findOwners(0, mbr.size() + 3 * sector + 1, false);
        QCOMPARE(owners.size(), 1);
        QCOMPARE(owners.first().toLower(), QStringLiteral("/second.txt"));
        QBuffer output;
        QVERIFY(output.open(QIODevice::WriteOnly));
        const auto transfer = partitioned->exportFile(
            0, deleted.first(), Evidence::MappingMethod::Retained, output);
        QVERIFY2(transfer.complete, qPrintable(transfer.error));
        QCOMPARE(transfer.writtenBytes, quint64(1));
        QCOMPARE(transfer.sha256,
                 QCryptographicHash::hash(QByteArray("D"), QCryptographicHash::Sha256).toHex());
        QCOMPARE(output.data(), QByteArray("D"));
        QBuffer cancelled;
        QVERIFY(cancelled.open(QIODevice::WriteOnly));
        const auto stopped = partitioned->exportFile(
            0, deleted.first(), Evidence::MappingMethod::Retained, cancelled,
            [](quint64, quint64) { return false; });
        QVERIFY(stopped.cancelled);
        QVERIFY(cancelled.data().isEmpty());
        const auto allocation = partitioned->allocationMapping(
            0, Evidence::Free | Evidence::Allocated | Evidence::Orphan);
        QVERIFY(allocation.status == Evidence::WalkStatus::Complete);
        QVERIFY(std::any_of(
            allocation.ranges.cbegin(), allocation.ranges.cend(), [&](const Evidence::Range &range) {
                return (range.flags & Evidence::Orphan)
                    && range.physical == quint64(mbr.size() + 4 * sector)
                    && range.length == sector;
            }));
        const auto names = partitioned->search(0, Evidence::SearchKind::Name, 0, "*", {});
        QVERIFY(names.status == Evidence::WalkStatus::Complete);
        QVERIFY(std::any_of(names.hits.cbegin(), names.hits.cend(), [](const auto &hit) {
            return hit.match.toLower() == QStringLiteral("/second.txt");
        }));
        const auto signatures
            = partitioned->search(0, Evidence::SearchKind::Signature, Evidence::Orphan, {}, {});
        QVERIFY(signatures.status == Evidence::WalkStatus::Complete);
        QCOMPARE(signatures.hits.size(), 1);
        QCOMPARE(signatures.hits.first().match, QStringLiteral("PNG"));
        QCOMPARE(signatures.hits.first().offset, quint64(mbr.size() + 4 * sector + 32));
        QCOMPARE(partitioned->validatedPngLength(signatures.hits.first()), quint64(png.data().size()));
        auto truncatedHit = signatures.hits.first();
        truncatedHit.rangeRemaining = quint64(png.data().size() - 1);
        QCOMPARE(partitioned->validatedPngLength(truncatedHit), quint64(0));
        const auto freeText
            = partitioned->search(0, Evidence::SearchKind::Text, Evidence::Free, "FREE", {});
        QVERIFY(freeText.status == Evidence::WalkStatus::Complete);
        QCOMPARE(freeText.hits.size(), 1);
        QCOMPARE(freeText.hits.first().offset, quint64(mbr.size() + 5 * sector));
        const auto across = partitioned->search(0, Evidence::SearchKind::Hex, Evidence::Free,
                                                "43 46", {});
        QVERIFY(across.status == Evidence::WalkStatus::Complete);
        QVERIFY(across.hits.isEmpty());
#endif
        QCOMPARE(partitioned->listDirectory(0, {}, &readable).size(), 1);
#endif

        ForensicsWidget widget(nullptr);
        widget.resize(1120, 780);
        widget.show();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(second.fileName())});
#if QT_VERSION >= QT_VERSION_CHECK(6, 16, 0)
        const QPointF dragPoint(10, 10);
#else
        const QPoint dragPoint(10, 10);
#endif
        QDragEnterEvent drag(dragPoint, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &drag);
        QVERIFY(drag.isAccepted());
        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &drop);
        QVERIFY(drop.isAccepted());
        const auto *navigation = widget.findChild<QTreeWidget *>("evidence");
        QCOMPARE(navigation->topLevelItemCount(), 1);
        auto *entries = widget.findChild<QTreeWidget *>("entries");
        QVERIFY(entries->dragEnabled());
        QCOMPARE(entries->topLevelItemCount(), 1);
        entries->setCurrentItem(entries->topLevelItem(0));
        auto *tabs = widget.findChild<QTabWidget *>("inspector");
        auto *hex = widget.findChild<QPlainTextEdit *>("fileHex");
        tabs->setCurrentWidget(hex);
        QVERIFY(hex->toPlainText().contains("58 59"));
        QApplication::processEvents();
        auto *viewport = entries->viewport();
        const QPoint pressPoint = entries->visualItemRect(entries->topLevelItem(0)).center();
        const QPoint movePoint = pressPoint + QPoint(QApplication::startDragDistance() + 8, 0);
        const auto gesture = [&](QEvent::Type type, const QPoint &at, Qt::MouseButton button,
                                 Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(at), QPointF(viewport->mapToGlobal(at)), button,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
        };
        const auto beginDrag = [&]() {
            gesture(QEvent::MouseButtonPress, pressPoint, Qt::LeftButton, Qt::LeftButton);
            gesture(QEvent::MouseMove, movePoint, Qt::NoButton, Qt::LeftButton);
        };
        beginDrag();
        gesture(QEvent::MouseButtonRelease, movePoint, Qt::LeftButton, Qt::NoButton);
        auto *status = widget.findChild<QLabel *>(QStringLiteral("status"), Qt::FindChildrenRecursively);
        QTRY_VERIFY(status->text().contains(QStringLiteral("ready to drag")));
        beginDrag();
        auto *outgoing = entries->findChild<QDrag *>();
        QVERIFY(outgoing);
        const auto urls = outgoing->mimeData()->urls();
        QCOMPARE(urls.size(), 1);
        QFile droppedFile(urls.first().toLocalFile());
        QVERIFY(droppedFile.open(QIODevice::ReadOnly));
        QCOMPARE(droppedFile.readAll(), QByteArray("XY"));
        gesture(QEvent::MouseButtonRelease, movePoint, Qt::LeftButton, Qt::NoButton);
        const QString preview = qEnvironmentVariable("IAITO_FORENSICS_PREVIEW");
        if (!preview.isEmpty()) {
            QApplication::processEvents();
            QVERIFY(widget.grab().save(preview));
        }

        QFile savedCase(temporary.filePath("saved.iaitocase"));
        QVERIFY(widget.saveCaseFile(savedCase.fileName(), &error));
        QVERIFY(savedCase.open(QIODevice::ReadOnly));
        auto document = QJsonDocument::fromJson(savedCase.readAll()).object();
        savedCase.close();
        QCOMPARE(document["schema"].toString(), QStringLiteral("iaito-forensics-case"));
        QCOMPARE(document["sources"].toArray().size(), 1);
        auto sourceRecord = document["sources"].toArray().first().toObject();
        sourceRecord["sha256"] = QString::fromLatin1(imageDigest);
        document["sources"] = QJsonArray{sourceRecord};
        document["bookmarks"] = QJsonArray{QJsonObject{
            {"kind", "range"}, {"image", partitioned->path()},
            {"name", "First file bytes"}, {"note", "Check export provenance"},
            {"sha256", QString::fromLatin1(imageDigest)},
            {"offset", QString::number(mbr.size() + 3 * sector)},
            {"length", "2"},
        }};
        QVERIFY(savedCase.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(savedCase.write(QJsonDocument(document).toJson()) > 0);
        savedCase.close();
        QVERIFY2(widget.openCaseFile(savedCase.fileName(), &error), qPrintable(error));
        auto *bookmarkTable = widget.findChild<QTreeWidget *>("bookmarks");
        QCOMPARE(bookmarkTable->topLevelItemCount(), 1);
        QCOMPARE(bookmarkTable->topLevelItem(0)->text(2), QStringLiteral("Check export provenance"));
        bookmarkTable->itemActivated(bookmarkTable->topLevelItem(0), 0);
        QTRY_COMPARE(tabs->currentWidget()->objectName(), QStringLiteral("imageHex"));
        QVERIFY(widget.findChild<QPlainTextEdit *>("imageHex")->toPlainText().contains("58 59"));
#if R2_ABIVERSION >= 188
        auto *allocationPage = widget.findChild<QWidget *>("allocation");
        tabs->setCurrentWidget(allocationPage);
        auto *scan = allocationPage->findChild<QPushButton *>("scan");
        QVERIFY(scan->isEnabled());
        QTest::mouseClick(scan, Qt::LeftButton);
        auto *ranges = allocationPage->findChild<QTableView *>("ranges");
        QTRY_VERIFY(ranges->model()->rowCount() >= 3);
        const QString allocationPreview = qEnvironmentVariable("IAITO_ALLOCATION_PREVIEW");
        if (!allocationPreview.isEmpty()) {
            QApplication::processEvents();
            QVERIFY(widget.grab().save(allocationPreview));
        }
#endif
        QVERIFY(!widget.saveCaseFile(second.fileName(), &error));
        QVERIFY(second.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(second.readAll(), QCryptographicHash::Sha256).toHex(),
                 imageDigest);

        widget.hide();
        checkSessionEvidence(first.fileName(), QByteArray(mbr.size(), '\0') + fat, mbr.size());
    }

private:
    void checkSessionEvidence(const QString &path, const QByteArray &embedded, quint64 offset)
    {
        constexpr quint64 mappedAddress = 0x100000;
        int originalFd;
        int fileFd = -1;
        int memoryFd = -1;
        int originalVa;
        {
            RCoreLocked core = Core()->core();
            originalFd = r_io_fd_get_current(core->io);
            originalVa = core->io->va;
        }
        const auto cleanup = qScopeGuard([&]() {
            RCoreLocked core = Core()->core();
            r_fs_umount(core->fs, "/mounted");
            r_io_fd_close(core->io, memoryFd);
            r_io_fd_close(core->io, fileFd);
            r_io_use_fd(core->io, originalFd);
            core->io->va = originalVa;
        });
        {
            RCoreLocked core = Core()->core();
            auto *file = r_io_open(core->io, QFile::encodeName(path).constData(), R_PERM_R, 0);
            QVERIFY(file);
            fileFd = file->fd;
            QVERIFY(r_io_use_fd(core->io, fileFd));
        }

        ForensicsWidget widget(nullptr);
        widget.resize(1120, 780);
        widget.show();
        auto *evidence = widget.findChild<QTreeWidget *>("evidence");
        auto *entries = widget.findChild<QTreeWidget *>("entries");
        QTRY_COMPARE(evidence->topLevelItemCount(), 1);
        QCOMPARE(entries->topLevelItemCount(), 1);
        QCOMPARE(entries->topLevelItem(0)->text(0).toLower(), QStringLiteral("first.txt"));

        {
            RCoreLocked core = Core()->core();
            const QByteArray uri = "malloc://" + QByteArray::number(embedded.size());
            auto *memory = r_io_open(core->io, uri.constData(), R_PERM_RW, 0);
            QVERIFY(memory);
            memoryFd = memory->fd;
            QVERIFY(r_io_use_fd(core->io, memoryFd));
            QCOMPARE(
                r_io_desc_write_at(
                    memory, 0, reinterpret_cast<const ut8 *>(embedded.constData()), embedded.size()),
                embedded.size());
            QVERIFY(r_io_map_add(
                core->io, memoryFd, R_PERM_R, offset, mappedAddress, embedded.size() - offset));
            core->io->va = true;
        }
        Core()->triggerRefreshAll();
        QCOMPARE(evidence->topLevelItemCount(), 0);
        {
            RCoreLocked core = Core()->core();
            QVERIFY(r_fs_mount(core->fs, "fat", "/mounted", mappedAddress));
        }
        emit Core() -> filesystemMountsChanged();
        QCOMPARE(evidence->topLevelItemCount(), 1);
        QVERIFY(evidence->currentItem()->text(0).startsWith(QStringLiteral("/mounted")));
        QCOMPARE(entries->topLevelItemCount(), 1);
        QCOMPARE(entries->topLevelItem(0)->text(0).toLower(), QStringLiteral("second.txt"));

#if R2_ABIVERSION >= 187
        auto *visibility = widget.findChild<QComboBox *>("visibleEntries");
        visibility->setCurrentIndex(
            visibility->findData(int(FilesystemEvidence::DirectoryView::Deleted)));
        QCoreApplication::processEvents();
        QStyleOptionComboBox combo;
        combo.initFrom(visibility);
        const QRect textArea = visibility->style()->subControlRect(
            QStyle::CC_ComboBox, &combo, QStyle::SC_ComboBoxEditField, visibility);
        QVERIFY(
            textArea.width()
            >= visibility->fontMetrics().horizontalAdvance(visibility->currentText()));
        QCOMPARE(entries->topLevelItemCount(), 1);
        QCOMPARE(entries->topLevelItem(0)->text(3), QStringLiteral("Deleted"));
        QTimer::singleShot(0, &widget, [&widget]() {
            auto *menu = widget.findChild<QMenu *>();
            if (!menu) {
                return;
            }
            for (auto *action : menu->actions()) {
                if (action->text() == QStringLiteral("Properties…")) {
                    action->trigger();
                    break;
                }
            }
            menu->close();
        });
        entries->customContextMenuRequested(
            entries->visualItemRect(entries->topLevelItem(0)).center());
        auto *dialog = widget.findChild<QDialog *>();
        QVERIFY(dialog && dialog->isVisible());
        QStringList properties;
        for (auto *label : dialog->findChildren<QLabel *>()) {
            properties.append(label->text());
        }
        QVERIFY(properties.contains(QStringLiteral("First image offset")));
        QVERIFY(properties.contains(RAddressString(offset + 4 * 512)));
        QVERIFY(properties.contains(QStringLiteral("Deleted")));
        dialog->close();
#endif
        {
            RCoreLocked core = Core()->core();
            QVERIFY(r_fs_umount(core->fs, "/mounted"));
        }
        emit Core() -> filesystemMountsChanged();
        QCOMPARE(evidence->topLevelItemCount(), 0);
        QCOMPARE(entries->topLevelItemCount(), 0);
    }

private slots:
    void classicExtEvidence()
    {
#if R2_ABIVERSION < 189
        QSKIP("The bound radare2 predates ext2 forensic support.");
#else
        QString mkfs = qEnvironmentVariable("IAITO_TEST_MKE2FS");
        QString debugfs = qEnvironmentVariable("IAITO_TEST_DEBUGFS");
        if (mkfs.isEmpty()) {
            mkfs = QStandardPaths::findExecutable("mke2fs");
        }
        if (debugfs.isEmpty()) {
            debugfs = QStandardPaths::findExecutable("debugfs");
        }
        if (mkfs.isEmpty() || debugfs.isEmpty()) {
            QSKIP("e2fsprogs mke2fs and debugfs are required for the ext2 fixture.");
        }
        QFile image(temporary.filePath("classic ext2.img"));
        QVERIFY(image.open(QIODevice::WriteOnly));
        QVERIFY(image.resize(1024 * 1024));
        image.close();
        const auto payload = QByteArray("classic ext2 data\n").repeated(1024);
        QFile input(temporary.filePath("ext2-payload"));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(payload), qint64(payload.size()));
        input.close();
        auto run = [this](const QString &program, const QStringList &arguments) {
            QProcess process;
            process.setWorkingDirectory(temporary.path());
            process.start(program, arguments);
            const bool ok = process.waitForFinished(10000)
                && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
            if (!ok) {
                qWarning() << process.errorString() << process.readAllStandardError();
            }
            return ok;
        };
        QVERIFY(run(mkfs, {"-q", "-F", "-t", "ext2", "-b", "1024", image.fileName()}));
        QVERIFY(run(debugfs, {"-w", "-R", "write ext2-payload /removed.bin", image.fileName()}));
        QVERIFY(run(debugfs, {"-w", "-R", "rm /removed.bin", image.fileName()}));

        QString error;
        const auto source = Core()->openFilesystemEvidence(image.fileName(), &error);
        QVERIFY2(source, qPrintable(error));
        QCOMPARE(source->volumes().size(), 1);
        QCOMPARE(source->volumes().first().filesystem, QStringLiteral("ext2"));
        if (!source->volumes().first().deletedEntries) {
            QSKIP("The linked ext2 filesystem driver lacks the classic recovery callbacks.");
        }
        using Evidence = FilesystemEvidence;
        bool readable;
        const auto deleted = source->listDirectory(0, {}, &readable, Evidence::DirectoryView::Deleted);
        QVERIFY(readable);
        const auto entry = std::find_if(deleted.cbegin(), deleted.cend(), [](const auto &candidate) {
            return candidate.name == QStringLiteral("removed.bin");
        });
        QVERIFY(entry != deleted.cend());
        QVERIFY(entry->deleted && entry->inferredName);
        const auto inodes
            = source->listDirectory(0, {}, &readable, Evidence::DirectoryView::FreeInodes);
        QVERIFY(readable);
        QVERIFY(std::any_of(inodes.cbegin(), inodes.cend(), [&](const Evidence::Entry &inode) {
            return inode.id == entry->id && inode.name == QStringLiteral("inode-%1").arg(entry->id);
        }));
        const auto mapping = source->fileMapping(0, *entry, Evidence::MappingMethod::Retained);
        QVERIFY(mapping.status == Evidence::WalkStatus::Complete);
        QVERIFY(mapping.ranges.size() > 1);
        QBuffer extracted;
        QVERIFY(extracted.open(QIODevice::WriteOnly));
        const auto transfer = source->exportFile(0, *entry, Evidence::MappingMethod::Retained, extracted);
        QVERIFY2(transfer.complete, qPrintable(transfer.error));
        QCOMPARE(extracted.data(), payload);
#endif
    }
};

QTEST_MAIN(FilesystemTest)
#include "test_filesystem.moc"
