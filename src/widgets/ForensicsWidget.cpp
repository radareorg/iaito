#include "widgets/ForensicsWidget.h"
#include "common/FileDialog.h"
#include "core/FilesystemEvidence.h"
#include "core/Iaito.h"
#include "ui_ForensicsWidget.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QStyle>

namespace {
constexpr int SourceRole = Qt::UserRole;
constexpr int VolumeRole = Qt::UserRole + 1;
constexpr int DirectoryRole = Qt::UserRole + 2;

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
    ui->entries->header()->setStretchLastSection(false);
    ui->up->setEnabled(false);
    ui->up->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    ui->openImage->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    connect(ui->openImage, &QPushButton::clicked, this, &ForensicsWidget::openImage);
    connect(ui->filter, &QLineEdit::textChanged, this, &ForensicsWidget::filterEntries);
    connect(ui->evidence, &QTreeWidget::currentItemChanged, this, &ForensicsWidget::showEvidenceItem);
    connect(ui->up, &QToolButton::clicked, this, [this]() {
        showDirectory(directory.left(directory.lastIndexOf('/')));
    });
    connect(ui->entries, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if (item->data(0, DirectoryRole).isValid()) {
            showDirectory(item->data(0, DirectoryRole).toByteArray());
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
    QStringList details{source->path(), byteCount(source->size()), tr("Read-only image")};
    if (volumeIndex >= 0) {
        const auto &volume = source->volumes().at(volumeIndex);
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
    ui->properties->setPlainText(details.join(QLatin1Char('\n')));
    showDirectory({});
}

void ForensicsWidget::showDirectory(const QByteArray &path)
{
    directory = path;
    ui->entries->clear();
    ui->up->setEnabled(!path.isEmpty());
    ui->location->setText(path.isEmpty() ? QStringLiteral("/") : QString::fromUtf8(path));
    if (volumeIndex < 0) {
        ui->status->setText(tr("Select a volume to browse its filesystem."));
        return;
    }
    const auto &source = sources.at(sourceIndex);
    bool readable;
    const auto entries = source->listDirectory(volumeIndex, path, &readable);
    for (const auto &entry : entries) {
        auto *item = new QTreeWidgetItem(
            ui->entries,
            {entry.name,
             entry.directory ? QString() : QLocale().toString(entry.size),
             entry.directory ? tr("Folder") : tr("File")});
        item->setIcon(
            0, style()->standardIcon(entry.directory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon));
        if (entry.directory) {
            item->setData(0, DirectoryRole, entry.path);
        }
    }
    ui->status->setText(
        readable ? tr("%n entries", nullptr, int(entries.size()))
                 : tr("The selected filesystem directory is unavailable."));
    filterEntries(ui->filter->text());
}

void ForensicsWidget::filterEntries(const QString &filter)
{
    for (int i = 0; i < ui->entries->topLevelItemCount(); ++i) {
        auto *item = ui->entries->topLevelItem(i);
        item->setHidden(!item->text(0).contains(filter, Qt::CaseInsensitive));
    }
}
