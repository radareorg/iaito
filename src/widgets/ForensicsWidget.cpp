#include "widgets/ForensicsWidget.h"
#include "common/FileDialog.h"
#include "core/FilesystemEvidence.h"
#include "core/Iaito.h"
#include "ui_ForensicsWidget.h"

#include <limits>
#include <QDateTime>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QStyle>

namespace {
constexpr int SourceRole = Qt::UserRole;
constexpr int VolumeRole = Qt::UserRole + 1;
constexpr int EntryRole = Qt::UserRole + 2;
constexpr int ImageOffsetRole = Qt::UserRole + 3;
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

ForensicsWidget::~ForensicsWidget() = default;

void ForensicsWidget::openImage()
{
    const QString path = Iaito::getOpenFileName(this, tr("Open Evidence Image"), {});
    if (path.isEmpty()) {
        return;
    }
    const QString canonical = QFileInfo(path).canonicalFilePath();
    for (int i = 0; i < sources.size(); ++i) {
        if (sources.at(i)->path() == canonical) {
            ui->evidence->setCurrentItem(ui->evidence->topLevelItem(i));
            return;
        }
    }
    QString error;
    auto source = Core()->openFilesystemEvidence(path, &error);
    if (!source) {
        QMessageBox::warning(this, tr("Open Evidence Image"), error);
        return;
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
    for (const auto &range : mapping.ranges) {
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
    if (mapping.status == Evidence::WalkStatus::Unsupported) {
        ui->extentStatus->setText(tr("This volume does not provide file extents."));
    } else if (entry.deleted) {
        ui->extentStatus->setText(tr("Deleted metadata can point to reused image ranges."));
    } else if (mapping.status != Evidence::WalkStatus::Complete) {
        ui->extentStatus->setText(tr("The file extent map is incomplete."));
    } else {
        ui->extentStatus->setText(tr("Activate an extent to open its image bytes."));
    }
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
