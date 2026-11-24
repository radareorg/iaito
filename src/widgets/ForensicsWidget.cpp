#include "widgets/ForensicsWidget.h"
#include "common/FileDialog.h"
#include "core/FilesystemEvidence.h"
#include "core/Iaito.h"
#include "ui_ForensicsWidget.h"

#include <algorithm>
#include <limits>
#include <QApplication>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QMimeData>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QStyle>
#include <QTemporaryDir>
#include <QTemporaryFile>

namespace {
constexpr int SourceRole = Qt::UserRole;
constexpr int VolumeRole = Qt::UserRole + 1;
constexpr int EntryRole = Qt::UserRole + 2;
constexpr int ImageOffsetRole = Qt::UserRole + 3;
constexpr int SearchHitRole = Qt::UserRole + 4;
constexpr int PageBytes = 4096;

QString byteCount(quint64 size)
{
    return QCoreApplication::translate("ForensicsWidget", "%1 bytes").arg(QLocale().toString(size));
}

QString hexPage(const QByteArray &bytes, quint64 offset)
{
    QString output;
    for (int i = 0; i < bytes.size(); i += 16) {
        const auto row = bytes.mid(i, 16);
        output += RAddressString(offset + i) + QStringLiteral("   ");
        output += QString::fromLatin1(row.toHex(' ')).leftJustified(47);
        output += QStringLiteral("   ");
        for (unsigned char byte : row) {
            output += QLatin1Char(byte >= 32 && byte <= 126 ? char(byte) : '.');
        }
        output += QLatin1Char('\n');
    }
    return output;
}

QString extentState(quint32 flags)
{
    using Range = FilesystemEvidence;
    const auto translate = [](const char *text) {
        return QCoreApplication::translate("ForensicsWidget", text);
    };
    if (flags & Range::Sparse) {
        return translate(QT_TRANSLATE_NOOP("ForensicsWidget", "Sparse hole"));
    }
    if (flags & Range::Unwritten) {
        return translate(QT_TRANSLATE_NOOP("ForensicsWidget", "Unwritten"));
    }
    if (flags & Range::Heuristic) {
        return translate(QT_TRANSLATE_NOOP("ForensicsWidget", "Heuristic"));
    }
    if (flags & Range::Free) {
        return translate(QT_TRANSLATE_NOOP("ForensicsWidget", "Free"));
    }
    if (flags & Range::Allocated) {
        return translate(QT_TRANSLATE_NOOP("ForensicsWidget", "Allocated"));
    }
    return {};
}
} // namespace

ForensicsWidget::ForensicsWidget(MainWindow *main)
    : IaitoDockWidget(main)
    , ui(new Ui::ForensicsWidget)
{
    setObjectName(QStringLiteral("ForensicsWidget"));
    setWindowTitle(tr("Forensics"));
    auto *contents = new QWidget(this);
    ui->setupUi(contents);
    setWidget(contents);
    setAcceptDrops(true);
    ui->entries->setDragHandler([this]() { dragSelectedFiles(); });
    ui->workspace->setStretchFactor(1, 1);
    ui->workspace->setSizes({230, 680});
    ui->inspection->setStretchFactor(0, 2);
    ui->inspection->setStretchFactor(1, 1);
    ui->entries->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->entries->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->entries->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->entries->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->entries->header()->setStretchLastSection(false);
    ui->up->setEnabled(false);
    ui->visibleEntries->setEnabled(false);
    ui->up->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    ui->openImage->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    const auto mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    ui->fileText->setFont(mono);
    ui->fileHex->setFont(mono);
    ui->imageHex->setFont(mono);
    ui->filePaging->hide();
    ui->progressPanel->hide();
    ui->exportFiles->setEnabled(false);
    ui->previousPage->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
    ui->nextPage->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    connect(ui->openImage, &QPushButton::clicked, this, &ForensicsWidget::openImage);
    connect(ui->filter, &QLineEdit::textChanged, this, &ForensicsWidget::filterEntries);
    connect(ui->evidence, &QTreeWidget::currentItemChanged, this, &ForensicsWidget::showEvidenceItem);
    connect(ui->entries, &QTreeWidget::currentItemChanged, this, &ForensicsWidget::showEntry);
    connect(ui->inspector, &QTabWidget::currentChanged, this, [this]() {
        updateFilePreview();
        updateExtents();
    });
    connect(ui->previousPage, &QToolButton::clicked, this, [this]() {
        auto &offset = ui->inspector->currentWidget() == ui->imageHex ? imageOffset : fileOffset;
        offset = offset > PageBytes ? offset - PageBytes : 0;
        updateFilePreview();
    });
    connect(ui->nextPage, &QToolButton::clicked, this, [this]() {
        auto &offset = ui->inspector->currentWidget() == ui->imageHex ? imageOffset : fileOffset;
        offset += PageBytes;
        updateFilePreview();
    });
    connect(ui->fileOffset, &QLineEdit::returnPressed, this, [this]() {
        bool ok;
        const quint64 offset = ui->fileOffset->text().trimmed().toULongLong(&ok, 0);
        if (ok) {
            (ui->inspector->currentWidget() == ui->imageHex ? imageOffset : fileOffset) = offset;
            updateFilePreview();
        } else {
            ui->filePageStatus->setText(tr("Enter a decimal or hexadecimal offset."));
        }
    });
    connect(ui->findOwners, &QPushButton::clicked, this, &ForensicsWidget::findOwners);
    connect(
        ui->allocation,
        &FilesystemAllocationView::scanRequested,
        this,
        &ForensicsWidget::scanAllocation);
    connect(
        ui->allocation,
        &FilesystemAllocationView::imageOffsetActivated,
        this,
        &ForensicsWidget::goToImageOffset);
    using SearchKind = FilesystemEvidence::SearchKind;
    ui->searchKind->addItem(tr("File name"), int(SearchKind::Name));
    ui->searchKind->addItem(tr("Text bytes"), int(SearchKind::Text));
    ui->searchKind->addItem(tr("Hex bytes"), int(SearchKind::Hex));
    ui->searchKind->addItem(tr("File signatures"), int(SearchKind::Signature));
    ui->searchScope->addItem(tr("Whole volume"), 0);
    ui->searchResults->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->searchResults->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->searchResults->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->searchResults->header()->setStretchLastSection(false);
    ui->exportHit->setEnabled(false);
    updateSearchControls();
    connect(
        ui->searchKind,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &ForensicsWidget::updateSearchControls);
    connect(ui->runSearch, &QPushButton::clicked, this, &ForensicsWidget::searchVolume);
    connect(ui->searchTerm, &QLineEdit::returnPressed, this, &ForensicsWidget::searchVolume);
    connect(ui->searchResults, &QTreeWidget::itemActivated, this, &ForensicsWidget::activateSearchHit);
    connect(ui->searchResults, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        ui->exportHit->setEnabled(
            item
            && item->data(0, SearchHitRole).value<FilesystemEvidence::SearchHit>().offset
                   != UT64_MAX);
    });
    connect(ui->exportHit, &QPushButton::clicked, this, &ForensicsWidget::exportSearchHit);
    using Method = FilesystemEvidence::MappingMethod;
    connect(ui->exportFiles, &QPushButton::clicked, this, [this]() {
        exportSelected(Method::Retained);
    });
    connect(ui->entries, &QTreeWidget::itemSelectionChanged, this, [this]() {
        ui->exportFiles->setEnabled(!job && !selectedFiles().isEmpty());
    });
    connect(ui->cancelJob, &QPushButton::clicked, this, [this]() {
        if (job) {
            job->interrupt();
            ui->cancelJob->setEnabled(false);
            ui->progressLabel->setText(tr("Cancelling…"));
        }
    });
    connect(ui->entries, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = ui->entries->itemAt(pos);
        if (!item) {
            return;
        }
        if (!item->isSelected()) {
            ui->entries->clearSelection();
            ui->entries->setCurrentItem(item);
        } else {
            ui->entries->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
        }
        QMenu menu(this);
        auto *exportAction = menu.addAction(tr("Export Selected…"));
        exportAction->setEnabled(!selectedFiles().isEmpty());
        connect(exportAction, &QAction::triggered, this, [this]() {
            exportSelected(Method::Retained);
        });
#if R2_ABIVERSION >= 188
        const auto entry = item->data(0, EntryRole).value<FilesystemEvidence::Entry>();
        if (entry.deleted && !entry.directory) {
            auto *preview = menu.addAction(tr("Preview Recovery"));
            connect(preview, &QAction::triggered, this, [this]() {
                checkRecovery(Method::Retained);
            });
            auto *guess = menu.addAction(tr("Preview Contiguous Estimate"));
            connect(guess, &QAction::triggered, this, [this]() {
                checkRecovery(Method::Contiguous);
            });
            auto *exportGuess = menu.addAction(tr("Export Contiguous Estimate…"));
            connect(exportGuess, &QAction::triggered, this, [this]() {
                exportSelected(Method::Contiguous);
            });
        }
#endif
        menu.exec(ui->entries->viewport()->mapToGlobal(pos));
    });
    connect(ui->extents, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const QVariant offset = item->data(0, ImageOffsetRole);
        if (offset.isValid()) {
            goToImageOffset(offset.toULongLong());
        }
    });
    connect(ui->visibleEntries, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        directories.clear();
        showDirectory();
    });
    connect(ui->up, &QToolButton::clicked, this, [this]() {
        if (!directories.isEmpty()) {
            directories.removeLast();
            showDirectory();
        }
    });
    connect(ui->entries, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        using View = FilesystemEvidence::DirectoryView;
        const auto entry = item->data(0, EntryRole).value<FilesystemEvidence::Entry>();
        if (entry.directory && View(ui->visibleEntries->currentData().toInt()) != View::FreeInodes) {
            directories.append(entry);
            showDirectory();
        }
    });
}

ForensicsWidget::~ForensicsWidget()
{
    if (job) {
        job->interrupt();
    }
}

void ForensicsWidget::openImage()
{
    if (job) {
        return;
    }
    const QString path = Iaito::getOpenFileName(this, tr("Open Evidence Image"), {});
    if (!path.isEmpty()) {
        QString error;
        if (!addImage(path, &error)) {
            QMessageBox::warning(this, tr("Open Evidence Image"), error);
        }
    }
}

bool ForensicsWidget::addImage(const QString &path, QString *error)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    for (int i = 0; i < sources.size(); ++i) {
        if (sources.at(i)->path() == canonical) {
            auto *image = ui->evidence->topLevelItem(i);
            ui->evidence->setCurrentItem(image->childCount() == 1 ? image->child(0) : image);
            return true;
        }
    }
    auto source = Core()->openFilesystemEvidence(path, error);
    if (!source) {
        return false;
    }
    const int index = int(sources.size());
    sources.append(source);
    auto *image = new QTreeWidgetItem(ui->evidence, {QFileInfo(source->path()).fileName()});
    image->setIcon(0, style()->standardIcon(QStyle::SP_DriveHDIcon));
    image->setToolTip(0, source->path());
    image->setData(0, SourceRole, index);
    const auto &volumes = source->volumes();
    for (int i = 0; i < volumes.size(); ++i) {
        const auto &volume = volumes.at(i);
        auto *item = new QTreeWidgetItem(image, {volume.partition});
        item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        item->setData(0, SourceRole, index);
        item->setData(0, VolumeRole, i);
        item->setToolTip(0, tr("%1 at %2").arg(volume.filesystem, RAddressString(volume.start)));
    }
    image->setExpanded(true);
    ui->evidence->setCurrentItem(volumes.size() == 1 ? image->child(0) : image);
    return true;
}

void ForensicsWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (job || !event->mimeData()->hasUrls()) {
        return;
    }
    const auto urls = event->mimeData()->urls();
    if (std::any_of(urls.cbegin(), urls.cend(), [](const QUrl &url) {
            return url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile();
        })) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

void ForensicsWidget::dropEvent(QDropEvent *event)
{
    if (job) {
        return;
    }
    bool opened = false;
    QString error;
    for (const auto &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            opened |= addImage(url.toLocalFile(), &error);
        }
    }
    if (!error.isEmpty()) {
        ui->status->setText(error);
    }
    if (opened) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

void ForensicsWidget::showEvidenceItem(QTreeWidgetItem *item)
{
    if (!item) {
        return;
    }
    sourceIndex = item->data(0, SourceRole).toInt();
    const auto &source = sources.at(sourceIndex);
    const QVariant selectedVolume = item->data(0, VolumeRole);
    volumeIndex = selectedVolume.isValid() ? selectedVolume.toInt() : -1;
    ui->allocation->setVolume(volumeIndex >= 0 ? &source->volumes().at(volumeIndex) : nullptr);
    ui->searchScope->clear();
    ui->searchScope->addItem(tr("Whole volume"), 0);
    if (volumeIndex >= 0 && source->volumes().at(volumeIndex).deletedEntries
        && R2_ABIVERSION >= 188) {
        ui->searchScope->addItem(tr("Free space"), uint(FilesystemEvidence::Free));
        ui->searchScope->addItem(tr("Orphan space"), uint(FilesystemEvidence::Orphan));
    }
    ui->searchResults->clear();
    updateSearchControls();
    imageOffset = volumeIndex >= 0 ? source->volumes().at(volumeIndex).start : 0;
    ui->owners->clear();
    const QSignalBlocker blockViewSignals(ui->visibleEntries);
    ui->visibleEntries->clear();
    using View = FilesystemEvidence::DirectoryView;
    ui->visibleEntries->addItem(tr("Live entries"), int(View::Live));
    ui->visibleEntries->setEnabled(false);
    QStringList details{source->path(), byteCount(source->size()), tr("Read-only image")};
    if (volumeIndex >= 0) {
        const auto &volume = source->volumes().at(volumeIndex);
        if (volume.deletedEntries) {
            ui->visibleEntries->addItem(tr("Live and deleted"), int(View::All));
            ui->visibleEntries->addItem(tr("Deleted entries"), int(View::Deleted));
            if (volume.filesystem == QLatin1String("ext2")) {
                ui->visibleEntries->addItem(tr("Free inodes"), int(View::FreeInodes));
            }
            ui->visibleEntries->setEnabled(true);
        }
        details << QString() << tr("Partition  %1").arg(volume.partition)
                << tr("Start  %1").arg(RAddressString(volume.start))
                << tr("Length  %1").arg(byteCount(volume.length))
                << tr("Filesystem  %1")
                       .arg(volume.filesystem.isEmpty() ? tr("Unknown") : volume.filesystem);
        const QString volumeDetails = source->volumeDetails(volumeIndex);
        if (!volumeDetails.isEmpty()) {
            details << QString() << volumeDetails.trimmed();
        }
    }
    volumeProperties = details.join(QLatin1Char('\n'));
    directories.clear();
    showDirectory();
}

void ForensicsWidget::showDirectory()
{
    const auto directory = directories.isEmpty() ? FilesystemEvidence::Entry() : directories.last();
    const auto &path = directory.path;
    ui->entries->clear();
    showEntry(nullptr);
    ui->up->setEnabled(!path.isEmpty());
    ui->location->setText(path.isEmpty() ? QStringLiteral("/") : QString::fromUtf8(path));
    if (volumeIndex < 0) {
        ui->status->setText(tr("Select a volume to browse its filesystem."));
        return;
    }
    const auto &source = sources.at(sourceIndex);
    bool readable;
    const auto view = FilesystemEvidence::DirectoryView(ui->visibleEntries->currentData().toInt());
    const auto entries = source->listDirectory(volumeIndex, path, &readable, view, directory.id);
    for (const auto &entry : entries) {
        const QString status = entry.conflict                        ? tr("Conflict")
                               : entry.deleted && entry.inferredName ? tr("Inferred")
                               : entry.deleted                       ? tr("Deleted")
                                                                     : QString();
        auto *item = new QTreeWidgetItem(
            ui->entries,
            {entry.name,
             entry.directory ? QString() : QLocale().toString(entry.size),
             entry.directory     ? tr("Folder")
             : entry.type == 'l' ? tr("Symbolic link")
                                 : tr("File"),
             status});
        item->setIcon(
            0, style()->standardIcon(entry.directory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon));
        item->setData(0, EntryRole, QVariant::fromValue(entry));
    }
    ui->status->setText(
        readable ? tr("%n entries", nullptr, int(entries.size()))
                 : tr("The selected filesystem directory is unavailable."));
    filterEntries(ui->filter->text());
}

void ForensicsWidget::showEntry(QTreeWidgetItem *item)
{
    fileOffset = 0;
    if (!item) {
        ui->properties->setPlainText(volumeProperties);
        updateFilePreview();
        updateExtents();
        return;
    }
    const auto entry = item->data(0, EntryRole).value<FilesystemEvidence::Entry>();
    QStringList properties{QString::fromUtf8(entry.path), byteCount(entry.size)};
    if (entry.id != UT64_MAX) {
        const auto &volume = sources.at(sourceIndex)->volumes().at(volumeIndex);
        properties
            << (volume.filesystem == QLatin1String("ext2")
                    ? tr("Inode  %1").arg(entry.id)
                    : tr("Entry ID  %1").arg(RAddressString(entry.id)));
        if (volume.filesystem == QLatin1String("ext2") && volume.deletedEntries) {
            properties << tr("Owner UID  %1").arg(entry.owner)
                       << tr("Group GID  %1").arg(entry.group)
                       << tr("Mode  %1").arg(entry.mode, 4, 8, QLatin1Char('0'));
        }
    }
    if (entry.firstOffset != UT64_MAX) {
        properties << tr("First image offset  %1").arg(RAddressString(entry.firstOffset));
    }
    if (entry.modified && entry.modified <= quint64(std::numeric_limits<qint64>::max())) {
        const auto time = QDateTime::fromSecsSinceEpoch(qint64(entry.modified)).toUTC();
        properties << tr("Modified  %1").arg(time.toString(Qt::ISODate));
    }
    if (entry.deleted) {
        properties << tr("Deleted entry");
    }
    if (entry.inferredName) {
        properties << tr("Name inferred from filesystem metadata");
    }
    if (entry.conflict) {
        properties << tr("Entry metadata conflicts with the current allocation state");
    }
    ui->properties->setPlainText(properties.join(QLatin1Char('\n')));
    updateFilePreview();
    updateExtents();
}

void ForensicsWidget::updateFilePreview()
{
    auto *tab = ui->inspector->currentWidget();
    const bool image = tab == ui->imageHex;
    const bool hex = image || tab == ui->fileHex;
    const bool content = hex || tab == ui->fileText;
    ui->filePaging->setVisible(content);
    if (!content) {
        return;
    }
    auto *preview = static_cast<QPlainTextEdit *>(tab);
    const quint64 offset = image ? imageOffset : fileOffset;
    preview->clear();
    ui->fileOffset->setText(RAddressString(offset));
    ui->fileOffset->setAccessibleName(image ? tr("Image offset") : tr("File offset"));
    ui->previousPage->setEnabled(false);
    ui->nextPage->setEnabled(false);
    ui->findOwners->setVisible(image);
    ui->findOwners->setEnabled(false);
    auto *item = ui->entries->currentItem();
    const auto entry = item ? item->data(0, EntryRole).value<FilesystemEvidence::Entry>()
                            : FilesystemEvidence::Entry();
    const bool hasFile = image ? sourceIndex >= 0 : item && !entry.directory;
    ui->fileOffset->setEnabled(hasFile);
    if (!hasFile) {
        ui->filePageStatus->setText(
            image ? tr("Open an image to inspect its bytes.")
                  : tr("Select a file to preview its contents."));
        return;
    }
    const auto &source = sources.at(sourceIndex);
    const auto page = image ? source->readImage(offset, PageBytes)
                            : source->readFile(volumeIndex, entry, offset, PageBytes);
    ui->findOwners->setEnabled(image && offset < page.size);
    if (!page.opened) {
        ui->filePageStatus->setText(tr("The selected file could not be opened."));
        return;
    }
    ui->previousPage->setEnabled(offset > 0);
    if (offset > page.size) {
        ui->filePageStatus->setText(tr("The offset is beyond the end."));
        return;
    }
    if (page.error) {
        ui->filePageStatus->setText(tr("Only %1 of %2 requested bytes were readable.")
                                        .arg(page.bytes.size())
                                        .arg(qMin<quint64>(PageBytes, page.size - offset)));
    } else {
        ui->filePageStatus->setText(tr("%1 of %2").arg(byteCount(offset), byteCount(page.size)));
        ui->nextPage->setEnabled(page.size - offset > PageBytes);
    }
    preview->setPlainText(hex ? hexPage(page.bytes, offset) : QString::fromUtf8(page.bytes));
}

void ForensicsWidget::goToImageOffset(quint64 offset)
{
    imageOffset = offset;
    if (ui->inspector->currentWidget() == ui->imageHex) {
        updateFilePreview();
    } else {
        ui->inspector->setCurrentWidget(ui->imageHex);
    }
}

void ForensicsWidget::updateExtents()
{
    if (ui->inspector->currentWidget() != ui->extentPage) {
        return;
    }
    ui->extents->clear();
    auto *item = ui->entries->currentItem();
    if (!item) {
        ui->extentStatus->setText(tr("Select an entry to inspect its extents."));
        return;
    }
    const auto entry = item->data(0, EntryRole).value<FilesystemEvidence::Entry>();
    using Evidence = FilesystemEvidence;
    const auto mapping = sources.at(sourceIndex)
                             ->fileMapping(volumeIndex, entry, Evidence::MappingMethod::Metadata);
    displayExtents(mapping, entry.deleted, false);
}

void ForensicsWidget::displayExtents(
    const FilesystemEvidence::Mapping &mapping, bool deleted, bool recovery)
{
    using Status = FilesystemEvidence::WalkStatus;
    ui->extents->clear();
    quint64 readable = 0;
    for (const auto &range : mapping.ranges) {
        if (range.length <= UT64_MAX - readable) {
            readable += range.length;
        }
        const bool physical = range.physical != UT64_MAX;
        auto *row = new QTreeWidgetItem(
            ui->extents,
            {RAddressString(range.logical),
             physical ? RAddressString(range.physical) : QStringLiteral("—"),
             QLocale().toString(range.length),
             extentState(range.flags)});
        if (physical) {
            row->setData(0, ImageOffsetRole, QVariant::fromValue(range.physical));
        }
    }
    if (mapping.status == Status::Unsupported) {
        ui->extentStatus->setText(tr("This volume does not provide file extents."));
    } else if (recovery) {
        QString summary
            = tr("%1 readable of %2").arg(byteCount(readable), byteCount(mapping.declaredBytes));
        if (mapping.heuristic) {
            summary += tr(" · Heuristic");
        }
        if (mapping.conflict) {
            summary += tr(" · Allocation conflict");
        }
        if (mapping.status != Status::Complete || mapping.partial) {
            summary += tr(" · Incomplete");
        }
        ui->extentStatus->setText(summary);
    } else if (deleted) {
        ui->extentStatus->setText(tr("Deleted metadata can point to reused image ranges."));
    } else if (mapping.status != Status::Complete) {
        ui->extentStatus->setText(tr("The file extent map is incomplete."));
    } else {
        ui->extentStatus->setText(tr("Activate an extent to open its image bytes."));
    }
}

void ForensicsWidget::checkRecovery(FilesystemEvidence::MappingMethod method)
{
    auto *item = ui->entries->currentItem();
    if (!item || job) {
        return;
    }
    const auto entry = item->data(0, EntryRole).value<FilesystemEvidence::Entry>();
    const auto source = sources.at(sourceIndex);
    const int index = volumeIndex;
    const auto result = std::make_shared<FilesystemEvidence::Mapping>();
    const auto ready = std::make_shared<bool>(false);
    startJob(
        tr("Checking recovery…"),
        [source, index, entry, method, result, ready](ForensicsTask &task) {
            if (!task.cancelled()) {
                *result = source->fileMapping(index, entry, method);
                *ready = !task.cancelled();
            }
        },
        [this, result, ready]() {
            if (*ready) {
                const QSignalBlocker blockTabSignals(ui->inspector);
                ui->inspector->setCurrentWidget(ui->extentPage);
                updateFilePreview();
                displayExtents(*result, true, true);
            }
        });
}

QList<FilesystemEvidence::Entry> ForensicsWidget::selectedFiles() const
{
    QList<FilesystemEvidence::Entry> entries;
    for (auto *item : ui->entries->selectedItems()) {
        const auto entry = item->data(0, EntryRole).value<FilesystemEvidence::Entry>();
        if (!item->isHidden() && !entry.directory && !entry.path.isEmpty()) {
#if R2_ABIVERSION < 188
            if (entry.deleted) {
                continue;
            }
#endif
            entries.append(entry);
        }
    }
    return entries;
}

void ForensicsWidget::startJob(
    const QString &title, ForensicsTask::Action work, std::function<void()> done)
{
    if (job) {
        return;
    }
    job = QSharedPointer<ForensicsTask>::create(title, std::move(work));
    ui->workspace->setEnabled(false);
    ui->filePaging->setEnabled(false);
    ui->openImage->setEnabled(false);
    ui->up->setEnabled(false);
    ui->visibleEntries->setEnabled(false);
    ui->exportFiles->setEnabled(false);
    ui->progressLabel->setText(title);
    ui->progress->setRange(0, 0);
    ui->cancelJob->setEnabled(true);
    ui->progressPanel->show();
    connect(
        job.data(),
        &ForensicsTask::progressChanged,
        this,
        [this](const QString &text, quint64 completed, quint64 total) {
            if (job && !job->cancelled()) {
                ui->progressLabel->setText(text);
                ui->progress->setRange(0, total ? 1000 : 0);
                if (total) {
                    ui->progress->setValue(int(1000 * double(completed) / double(total)));
                }
            }
        });
    connect(job.data(), &ForensicsTask::finished, this, [this, done = std::move(done)]() {
        const bool cancelled = job->cancelled();
        job.clear();
        ui->workspace->setEnabled(true);
        ui->filePaging->setEnabled(true);
        ui->openImage->setEnabled(true);
        ui->up->setEnabled(!directories.isEmpty());
        ui->visibleEntries->setEnabled(ui->visibleEntries->count() > 1);
        ui->exportFiles->setEnabled(!selectedFiles().isEmpty());
        ui->progressPanel->hide();
        if (cancelled) {
            ui->status->setText(tr("Operation cancelled."));
        }
        if (done) {
            done();
        }
    });
    Core()->getAsyncTaskManager()->start(job);
}

void ForensicsWidget::exportSelected(FilesystemEvidence::MappingMethod method)
{
    const auto entries = selectedFiles();
    if (job || entries.isEmpty()) {
        return;
    }
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Export Evidence Files"));
    if (folder.isEmpty()) {
        return;
    }
    const auto source = sources.at(sourceIndex);
    const int index = volumeIndex;
    auto summary = std::make_shared<QString>();
    startJob(
        tr("Exporting files…"),
        [source, index, entries, method, folder, summary](ForensicsTask &task) {
            const QDir directory(folder);
            QJsonArray results;
            int saved = 0;
            int partial = 0;
            for (const auto &entry : entries) {
                if (task.cancelled()) {
                    break;
                }
                const QString progress = tr("Exporting %1").arg(entry.name);
                task.report(progress);
                QTemporaryFile output(directory.filePath(QStringLiteral(".iaito-export-XXXXXX")));
                FilesystemEvidence::Transfer transfer;
                QString path;
                if (output.open()) {
                    transfer = source->exportFile(
                        index,
                        entry,
                        method,
                        output,
                        [&task, &progress](quint64 done, quint64 total) {
                            task.report(progress, done, total);
                            return !task.cancelled();
                        });
                    if (!transfer.cancelled && !transfer.writeError
                        && (transfer.complete || transfer.writtenBytes)) {
                        const QString name = transfer.complete
                                                 ? entry.name
                                                 : entry.name + QStringLiteral(".partial");
                        const QString destination = FilesystemDrag::uniquePath(directory, name);
                        if (output.flush() && output.rename(destination)) {
                            output.setAutoRemove(false);
                            path = QFileInfo(destination).fileName();
                            ++saved;
                            partial += !transfer.complete;
                        } else {
                            transfer.error = output.errorString();
                        }
                    }
                } else {
                    transfer.error = output.errorString();
                }
                results.append(
                    QJsonObject{
                        {"path", QString::fromUtf8(entry.path)},
                        {"entryId", entry.id == UT64_MAX ? QString() : QString::number(entry.id)},
                        {"declaredBytes", QString::number(transfer.declaredBytes)},
                        {"exportedBytes",
                         QString::number(path.isEmpty() ? 0 : transfer.writtenBytes)},
                        {"sha256",
                         path.isEmpty() ? QString() : QString::fromLatin1(transfer.sha256)},
                        {"output", path},
                        {"deleted", entry.deleted},
                        {"complete", transfer.complete && !path.isEmpty()},
                        {"heuristic", transfer.heuristic},
                        {"conflict", transfer.conflict},
                        {"cancelled", transfer.cancelled},
                        {"error", transfer.error},
                    });
            }
            const auto &volume = source->volumes().at(index);
            const bool heuristic = method == FilesystemEvidence::MappingMethod::Contiguous;
            const QJsonObject manifest{
                {"image", source->path()},
                {"imageSize", QString::number(source->size())},
                {"volumeOffset", QString::number(volume.start)},
                {"filesystem", volume.filesystem},
                {"method", heuristic ? "contiguous estimate" : "retained mapping"},
                {"exportedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                {"cancelled", task.cancelled()},
                {"files", results},
            };
            const QString manifestPath
                = FilesystemDrag::uniquePath(directory, QStringLiteral("manifest.json"));
            QSaveFile metadata(manifestPath);
            const auto bytes = QJsonDocument(manifest).toJson();
            const bool hasManifest = metadata.open(QIODevice::WriteOnly)
                                     && metadata.write(bytes) == bytes.size() && metadata.commit();
            *summary = tr("%1 exported, %2 partial, %3 without output.")
                           .arg(saved)
                           .arg(partial)
                           .arg(entries.size() - saved);
            *summary += hasManifest ? tr(" Manifest saved to %1.").arg(manifestPath)
                                    : tr(" The manifest could not be saved.");
        },
        [this, summary]() { ui->status->setText(*summary); });
}

void ForensicsWidget::dragSelectedFiles()
{
    const auto entries = selectedFiles();
    if (job || entries.isEmpty()) {
        return;
    }
    const auto source = sources.at(sourceIndex);
    const int index = volumeIndex;
    QList<QByteArray> key{
        source->path().toUtf8(),
        QByteArray::number(index),
        QByteArray::number(QFileInfo(source->path()).lastModified().toMSecsSinceEpoch()),
    };
    for (const auto &entry : entries) {
        key.append(entry.path);
        key.append(QByteArray::number(entry.id));
        key.append(entry.deleted ? "deleted" : "live");
    }
    if (dragSelection == key && !draggedPaths.isEmpty()) {
        FilesystemDrag::copyFiles(ui->entries, draggedPaths);
        return;
    }
    const auto temporary = std::make_shared<QTemporaryDir>();
    if (!temporary->isValid()) {
        ui->status->setText(tr("Could not create temporary files for the drag."));
        return;
    }
    const auto paths = std::make_shared<QStringList>();
    startJob(
        tr("Preparing files for drag…"),
        [source, index, entries, temporary, paths](ForensicsTask &task) {
            const QDir directory(temporary->path());
            for (const auto &entry : entries) {
                if (task.cancelled()) {
                    return;
                }
                const QString message = tr("Preparing %1").arg(entry.name);
                task.report(message);
                QTemporaryFile output(directory.filePath(QStringLiteral(".iaito-drag-XXXXXX")));
                if (!output.open()) {
                    continue;
                }
                const auto transfer = source->exportFile(
                    index,
                    entry,
                    FilesystemEvidence::MappingMethod::Retained,
                    output,
                    [&task, &message](quint64 done, quint64 total) {
                        task.report(message, done, total);
                        return !task.cancelled();
                    });
                if (transfer.cancelled || transfer.writeError
                    || (!transfer.complete && !transfer.writtenBytes)) {
                    continue;
                }
                const QString name = transfer.complete ? entry.name : entry.name + ".partial";
                const QString destination = FilesystemDrag::uniquePath(directory, name);
                if (output.flush() && output.rename(destination)) {
                    output.setAutoRemove(false);
                    paths->append(destination);
                }
            }
            if (task.cancelled()) {
                paths->clear();
            }
        },
        [this, temporary, key, paths]() {
            if (paths->isEmpty()) {
                ui->status->setText(tr("No files were prepared for dragging."));
                return;
            }
            dragDirectories.append(temporary);
            dragSelection = key;
            draggedPaths = *paths;
            if ((QApplication::mouseButtons() & Qt::LeftButton)
                && FilesystemDrag::copyFiles(ui->entries, draggedPaths) == Qt::CopyAction) {
                ui->status->setText(tr("%n files copied", nullptr, int(paths->size())));
            } else {
                ui->status->setText(tr("%n files ready to drag", nullptr, int(paths->size())));
            }
        });
}

void ForensicsWidget::scanAllocation()
{
    if (volumeIndex < 0 || job) {
        return;
    }
    using Evidence = FilesystemEvidence;
    const auto source = sources.at(sourceIndex);
    const int index = volumeIndex;
    const auto result = std::make_shared<Evidence::Mapping>();
    startJob(
        tr("Scanning allocation…"),
        [source, index, result](ForensicsTask &task) {
            const auto report = [&task](quint64 done, quint64 total) {
                task.report(tr("Scanning allocation…"), done, total);
                return !task.cancelled();
            };
            constexpr quint32 all = Evidence::Free | Evidence::Allocated | Evidence::Orphan
                                    | Evidence::Bad | Evidence::Unknown;
            *result = source->allocationMapping(index, all, report);
        },
        [this, result]() { ui->allocation->showMapping(*result); });
}

void ForensicsWidget::updateSearchControls()
{
    using Kind = FilesystemEvidence::SearchKind;
    const auto kind = Kind(ui->searchKind->currentData().toInt());
    const bool volume = volumeIndex >= 0;
    const bool names = kind == Kind::Name;
    const bool signatures = kind == Kind::Signature;
    ui->runSearch->setEnabled(volume);
    ui->searchTerm->setEnabled(volume && !signatures);
    ui->searchScope->setEnabled(volume && !names && ui->searchScope->count() > 1);
    ui->searchTerm->setPlaceholderText(
        names               ? tr("Name or glob")
        : kind == Kind::Hex ? tr("Hexadecimal bytes")
        : signatures        ? tr("PNG, JPEG, PDF, ZIP, GIF, ELF")
                            : tr("Text to find"));
    ui->searchStatus->setText(volume ? QString() : tr("Select a volume to search."));
}

void ForensicsWidget::searchVolume()
{
    if (volumeIndex < 0 || job) {
        return;
    }
    using Evidence = FilesystemEvidence;
    const auto kind = Evidence::SearchKind(ui->searchKind->currentData().toInt());
    const quint32 scope = ui->searchScope->currentData().toUInt();
    const QString term = ui->searchTerm->text();
    const auto source = sources.at(sourceIndex);
    const int index = volumeIndex;
    const auto result = std::make_shared<Evidence::SearchResult>();
    ui->searchResults->clear();
    startJob(
        tr("Searching evidence…"),
        [source, index, kind, scope, term, result](ForensicsTask &task) {
            *result = source->search(index, kind, scope, term, [&task](quint64 done, quint64 total) {
                task.report(tr("Searching evidence…"), done, total);
                return !task.cancelled();
            });
        },
        [this, result]() { showSearchResults(*result); });
}

void ForensicsWidget::showSearchResults(const FilesystemEvidence::SearchResult &result)
{
    using Evidence = FilesystemEvidence;
    ui->searchResults->clear();
    for (const auto &hit : result.hits) {
        const bool address = hit.offset != UT64_MAX;
        const QString scope = address       ? (hit.flags & Evidence::Orphan ? tr("Orphan space")
                                               : hit.flags & Evidence::Free ? tr("Free space")
                                                                            : tr("Volume"))
                              : hit.deleted ? tr("Deleted claim")
                                            : tr("Live entry");
        auto *row = new QTreeWidgetItem(
            ui->searchResults,
            {hit.match, address ? RAddressString(hit.offset) : QStringLiteral("—"), scope});
        row->setData(0, SearchHitRole, QVariant::fromValue(hit));
    }
    QString status = tr("%n matches", nullptr, int(result.hits.size()));
    if (!result.error.isEmpty()) {
        status = result.error;
    } else if (result.limited) {
        status += tr(" · Result limit reached");
    } else if (result.status == Evidence::WalkStatus::Unsupported) {
        status = tr("The selected scope is unavailable.");
    } else if (result.status != Evidence::WalkStatus::Complete) {
        status += tr(" · Search incomplete");
    }
    ui->searchStatus->setText(status);
}

void ForensicsWidget::activateSearchHit(QTreeWidgetItem *item)
{
    if (!item || volumeIndex < 0) {
        return;
    }
    const auto hit = item->data(0, SearchHitRole).value<FilesystemEvidence::SearchHit>();
    if (hit.offset != UT64_MAX) {
        goToImageOffset(hit.offset);
        return;
    }
    using Evidence = FilesystemEvidence;
    using View = Evidence::DirectoryView;
    const auto view = hit.deleted ? View::All : View::Live;
    const auto &source = sources.at(sourceIndex);
    QList<Evidence::Entry> parents;
    const auto parts = hit.path.mid(1).split('/');
    Evidence::Entry parent;
    for (int i = 0; i + 1 < parts.size(); ++i) {
        const QByteArray path = parent.path + '/' + parts.at(i);
        const auto entries
            = source->listDirectory(volumeIndex, parent.path, nullptr, view, parent.id);
        const auto it
            = std::find_if(entries.cbegin(), entries.cend(), [&path](const Evidence::Entry &entry) {
                  return entry.directory && entry.path == path;
              });
        if (it == entries.cend()) {
            ui->searchStatus->setText(tr("Cannot open the result’s parent directory."));
            return;
        }
        parents.append(*it);
        parent = *it;
    }
    const QSignalBlocker blockViewSignals(ui->visibleEntries);
    ui->visibleEntries->setCurrentIndex(ui->visibleEntries->findData(int(view)));
    ui->filter->clear();
    directories = parents;
    ui->inspector->setCurrentWidget(ui->properties);
    showDirectory();
    for (int i = 0; i < ui->entries->topLevelItemCount(); ++i) {
        auto *row = ui->entries->topLevelItem(i);
        const auto entry = row->data(0, EntryRole).value<Evidence::Entry>();
        if (entry.path == hit.path && entry.deleted == hit.deleted) {
            ui->entries->setCurrentItem(row);
            ui->entries->scrollToItem(row);
            ui->entries->setFocus();
            break;
        }
    }
}

void ForensicsWidget::exportSearchHit()
{
    auto *item = ui->searchResults->currentItem();
    if (!item || job) {
        return;
    }
    const auto hit = item->data(0, SearchHitRole).value<FilesystemEvidence::SearchHit>();
    if (hit.offset == UT64_MAX) {
        return;
    }
    bool accepted;
    const QString requested = QInputDialog::getText(
        this,
        tr("Export Image Range"),
        tr("Bytes to export from %1 (up to %2)")
            .arg(RAddressString(hit.offset))
            .arg(QLocale().toString(hit.rangeRemaining)),
        QLineEdit::Normal,
        QString::number(hit.length),
        &accepted);
    if (!accepted) {
        return;
    }
    bool valid;
    const quint64 length = requested.trimmed().toULongLong(&valid, 0);
    if (!valid || !length || length > hit.rangeRemaining) {
        ui->searchStatus->setText(tr("Enter a length within the matching range."));
        return;
    }
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Export Image Range"));
    if (folder.isEmpty()) {
        return;
    }
    const auto source = sources.at(sourceIndex);
    const auto result = std::make_shared<QString>();
    startJob(
        tr("Exporting image range…"),
        [source, hit, folder, length, result](ForensicsTask &task) {
            const QDir directory(folder);
            const QString path = FilesystemDrag::uniquePath(
                directory, QStringLiteral("range-%1.bin").arg(RAddressString(hit.offset)));
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly)) {
                *result = output.errorString();
                return;
            }
            const auto transfer = source->exportImageRange(
                hit.offset, length, output, [&task](quint64 done, quint64 total) {
                    task.report(tr("Exporting image range…"), done, total);
                    return !task.cancelled();
                });
            if (!transfer.complete || task.cancelled()) {
                *result = transfer.error;
                return;
            }
            if (!output.commit()) {
                *result = output.errorString();
                return;
            }
            const QJsonObject exportInfo{
                {"image", source->path()},
                {"offset", QString::number(hit.offset)},
                {"length", QString::number(length)},
                {"output", QFileInfo(path).fileName()},
                {"sha256", QString::fromLatin1(transfer.sha256)},
                {"match", hit.match},
            };
            QSaveFile manifest(
                FilesystemDrag::uniquePath(directory, QFileInfo(path).fileName() + ".json"));
            const auto data = QJsonDocument(exportInfo).toJson();
            const bool recorded = manifest.open(QIODevice::WriteOnly)
                                  && manifest.write(data) == data.size() && manifest.commit();
            *result = recorded
                          ? tr("Range exported to %1.").arg(path)
                          : tr("Range exported to %1. The manifest could not be saved.").arg(path);
        },
        [this, result]() {
            if (!result->isEmpty()) {
                ui->status->setText(*result);
            }
        });
}

void ForensicsWidget::findOwners()
{
    if (sourceIndex < 0) {
        return;
    }
    ui->owners->clear();
    const auto &source = sources.at(sourceIndex);
    const auto &volumes = source->volumes();
    for (int i = 0; i < volumes.size(); ++i) {
        const auto &volume = volumes.at(i);
        for (bool deleted : {false, true}) {
            if (deleted && !volume.deletedEntries) {
                continue;
            }
            for (const auto &owner : source->findOwners(i, imageOffset, deleted)) {
                new QTreeWidgetItem(
                    ui->owners,
                    {owner, volume.partition, deleted ? tr("Deleted claim") : tr("Live entry")});
            }
        }
    }
    ui->ownerStatus->setText(tr("%n entries at %1", nullptr, ui->owners->topLevelItemCount())
                                 .arg(RAddressString(imageOffset)));
    ui->inspector->setCurrentWidget(ui->ownerPage);
}

void ForensicsWidget::filterEntries(const QString &filter)
{
    for (int i = 0; i < ui->entries->topLevelItemCount(); ++i) {
        auto *item = ui->entries->topLevelItem(i);
        item->setHidden(!item->text(0).contains(filter, Qt::CaseInsensitive));
    }
}
