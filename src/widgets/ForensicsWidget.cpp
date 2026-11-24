#include "widgets/ForensicsWidget.h"
#include "common/FileDialog.h"
#include "core/FilesystemEvidence.h"
#include "core/Iaito.h"
#include "ui_ForensicsWidget.h"

#include <limits>
#include <QDateTime>
#include <QFileInfo>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QStyle>

namespace {
constexpr int SourceRole = Qt::UserRole;
constexpr int VolumeRole = Qt::UserRole + 1;
constexpr int EntryRole = Qt::UserRole + 2;

QString byteCount(quint64 size)
{
    return QCoreApplication::translate("ForensicsWidget", "%1 bytes").arg(QLocale().toString(size));
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
    connect(ui->openImage, &QPushButton::clicked, this, &ForensicsWidget::openImage);
    connect(ui->filter, &QLineEdit::textChanged, this, &ForensicsWidget::filterEntries);
    connect(ui->evidence, &QTreeWidget::currentItemChanged, this, &ForensicsWidget::showEvidenceItem);
    connect(ui->entries, &QTreeWidget::currentItemChanged, this, &ForensicsWidget::showEntry);
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
    if (!item) {
        ui->properties->setPlainText(volumeProperties);
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
}

void ForensicsWidget::filterEntries(const QString &filter)
{
    for (int i = 0; i < ui->entries->topLevelItemCount(); ++i) {
        auto *item = ui->entries->topLevelItem(i);
        item->setHidden(!item->text(0).contains(filter, Qt::CaseInsensitive));
    }
}
