#include "common/FileDialog.h"
#include "ui_ForensicsWidget.h"
#include "widgets/ForensicsWidget.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QMessageBox>
#include <QSaveFile>
#include <QSignalBlocker>

namespace {
constexpr int BookmarkRole = Qt::UserRole + 10;
const QString CaseFilter = QStringLiteral("iaito Case (*.iaitocase);;JSON (*.json)");

bool writeDocument(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile output(path);
    if (output.open(QIODevice::WriteOnly) && output.write(data) == data.size() && output.commit()) {
        return true;
    }
    if (error) {
        *error = output.errorString();
    }
    return false;
}

QByteArray csvField(QString field)
{
    const QString trimmed = field.trimmed();
    if (!trimmed.isEmpty() && QStringLiteral("=+-@").contains(trimmed.at(0))) {
        field.prepend(QLatin1Char('\''));
    }
    field.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return '"' + field.toUtf8() + '"';
}
} // namespace

void ForensicsWidget::recordCaseEvent(const QString &action, const QJsonObject &details)
{
    QJsonObject event = details;
    event.insert(QStringLiteral("action"), action);
    event.insert(QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    caseEvents.append(event);
    caseDirty = true;
}

QByteArray ForensicsWidget::recordedHash(const QString &path) const
{
    for (const auto &value : caseSources) {
        const auto source = value.toObject();
        if (source.value(QStringLiteral("path")).toString() == path) {
            return source.value(QStringLiteral("sha256")).toString().toLatin1();
        }
    }
    return {};
}

QByteArray ForensicsWidget::verifiedHash(const QString &path) const
{
    const auto modified = verifiedSourceTimes.value(path);
    return modified.isValid() && modified == QFileInfo(path).lastModified() ? recordedHash(path)
                                                                            : QByteArray();
}

void ForensicsWidget::verifySource(int index, std::function<void(const QByteArray &)> done)
{
    if (index < 0 || job) {
        return;
    }
    const auto source = sources.at(index);
    const auto digest = std::make_shared<QByteArray>();
    const QDateTime modified = QFileInfo(source->path()).lastModified();
    const QString title = tr("Hashing evidence image…");
    startJob(
        title,
        [source, title, digest](ForensicsTask &task) {
            *digest = source->imageSha256([&task, &title](quint64 done, quint64 total) {
                task.report(title, done, total);
                return !task.cancelled();
            });
            if (task.cancelled()) {
                digest->clear();
            }
        },
        [this, source, digest, modified, done = std::move(done)]() {
            if (digest->isEmpty() || QFileInfo(source->path()).lastModified() != modified) {
                ui->caseStatus->setText(tr("Could not verify the image."));
                return;
            }
            const auto expected = recordedHash(source->path());
            if (!expected.isEmpty() && expected != *digest) {
                ui->caseStatus->setText(tr("The image hash differs from the case record."));
                recordCaseEvent(
                    "sourceHashMismatch",
                    {{"image", source->path()},
                     {"expected", QString::fromLatin1(expected)},
                     {"observed", QString::fromLatin1(*digest)}});
                return;
            }
            for (int i = 0; i < caseSources.size(); ++i) {
                auto item = caseSources.at(i).toObject();
                if (item["path"].toString() == source->path()) {
                    item["sha256"] = QString::fromLatin1(*digest);
                    caseSources.replace(i, item);
                    break;
                }
            }
            verifiedSourceTimes.insert(source->path(), modified);
            recordCaseEvent(
                "sourceVerified",
                {{"image", source->path()}, {"sha256", QString::fromLatin1(*digest)}});
            ui->caseStatus->setText(tr("SHA-256 %1").arg(QString::fromLatin1(*digest)));
            if (done) {
                done(*digest);
            }
        });
}

void ForensicsWidget::bookmarkEntry()
{
    auto *item = ui->entries->currentItem();
    if (!item || volumeIndex < 0) {
        return;
    }
    const auto entry = item->data(0, EntryRole).value<FilesystemEvidence::Entry>();
    QJsonObject bookmark{
        {"kind", "entry"},
        {"name", entry.name},
        {"path", QString::fromUtf8(entry.path)},
        {"pathBytes", QString::fromLatin1(entry.path.toBase64())},
        {"length", QString::number(entry.size)},
        {"entryId", entry.id == UT64_MAX ? QString() : QString::number(entry.id)},
        {"offset", entry.firstOffset == UT64_MAX ? QString() : QString::number(entry.firstOffset)},
        {"deleted", entry.deleted},
        {"method", entry.deleted ? "deleted metadata" : "live path"},
    };
    addBookmark(bookmark);
}

void ForensicsWidget::bookmarkRange()
{
    if (sourceIndex < 0) {
        return;
    }
    const auto &source = sources.at(sourceIndex);
    if (imageOffset >= source->size()) {
        return;
    }
    bool confirmed;
    const quint64 remaining = source->size() - imageOffset;
    const auto lengthText = QInputDialog::getText(
        this,
        tr("Bookmark Image Range"),
        tr("Length in bytes"),
        QLineEdit::Normal,
        QString::number(qMin<quint64>(4096, remaining)),
        &confirmed);
    if (!confirmed) {
        return;
    }
    bool valid;
    const quint64 length = lengthText.trimmed().toULongLong(&valid, 0);
    if (!valid || !length || length > remaining) {
        ui->status->setText(tr("Enter a range within the image."));
        return;
    }
    addBookmark(
        {{"kind", "range"},
         {"name", RAddressString(imageOffset)},
         {"offset", QString::number(imageOffset)},
         {"length", QString::number(length)},
         {"method", "image bytes"}});
}

void ForensicsWidget::addBookmark(QJsonObject bookmark)
{
    if (job || sourceIndex < 0) {
        return;
    }
    bool confirmed;
    const QString note
        = QInputDialog::getMultiLineText(this, tr("Add Bookmark"), tr("Case note"), {}, &confirmed);
    if (!confirmed) {
        return;
    }
    const auto &source = sources.at(sourceIndex);
    bookmark.insert("note", note);
    bookmark.insert("image", source->path());
    bookmark.insert("imageSize", QString::number(source->size()));
    if (volumeIndex >= 0) {
        const auto &volume = source->volumes().at(volumeIndex);
        bookmark.insert("volumeOffset", QString::number(volume.start));
        bookmark.insert("filesystem", volume.filesystem);
    }
    auto commit = [this, bookmark](const QByteArray &digest) mutable {
        bookmark.insert("sha256", QString::fromLatin1(digest));
        bookmark.insert("created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        bookmarks.append(bookmark);
        recordCaseEvent(
            "bookmarkAdded", {{"image", bookmark.value("image")}, {"name", bookmark.value("name")}});
        showBookmarks();
        ui->inspector->setCurrentWidget(ui->bookmarkPage);
    };
    const auto digest = verifiedHash(source->path());
    if (!digest.isEmpty()) {
        commit(digest);
    } else {
        verifySource(sourceIndex, std::move(commit));
    }
}

void ForensicsWidget::showBookmarks()
{
    ui->bookmarks->clear();
    for (int i = 0; i < bookmarks.size(); ++i) {
        const auto bookmark = bookmarks.at(i).toObject();
        const auto source = bookmark["image"].toString();
        auto *item = new QTreeWidgetItem(
            ui->bookmarks,
            {bookmark["name"].toString(),
             QFileInfo(source).fileName(),
             bookmark["note"].toString()});
        item->setData(0, BookmarkRole, i);
        item->setToolTip(1, source);
        item->setToolTip(2, bookmark["note"].toString());
    }
    ui->caseStatus->setText(tr("%n bookmarks", nullptr, int(bookmarks.size())));
}

void ForensicsWidget::activateBookmark(QTreeWidgetItem *item)
{
    if (!item || job) {
        return;
    }
    const auto bookmark = bookmarks.at(item->data(0, BookmarkRole).toInt()).toObject();
    QString error;
    if (!addImage(bookmark["image"].toString(), &error)) {
        ui->caseStatus->setText(tr("Evidence source unavailable. %1").arg(error));
        return;
    }
    const int image = sourceIndex;
    const auto navigate = [this, image, bookmark](const QByteArray &digest) {
        if (digest != bookmark["sha256"].toString().toLatin1()) {
            ui->caseStatus->setText(tr("The image hash differs from the bookmark."));
            return;
        }
        bool hasVolume;
        const quint64 partition = bookmark["volumeOffset"].toString().toULongLong(&hasVolume);
        if (hasVolume) {
            const auto &volumes = sources.at(image)->volumes();
            int index = 0;
            while (index < volumes.size() && volumes.at(index).start != partition) {
                ++index;
            }
            if (index >= volumes.size()) {
                ui->caseStatus->setText(tr("The bookmarked volume is unavailable."));
                return;
            }
            ui->evidence->setCurrentItem(ui->evidence->topLevelItem(image)->child(index));
        }
        if (bookmark["kind"].toString() == "entry") {
            FilesystemEvidence::SearchHit hit;
            hit.path = QByteArray::fromBase64(bookmark["pathBytes"].toString().toLatin1());
            hit.deleted = bookmark["deleted"].toBool();
            bool hasId;
            const quint64 id = bookmark["entryId"].toString().toULongLong(&hasId);
            openSearchPath(hit, hasId ? id : UT64_MAX);
        } else {
            goToImageOffset(bookmark["offset"].toString().toULongLong());
        }
    };
    const auto path = sources.at(image)->path();
    const auto digest = verifiedHash(path);
    if (!digest.isEmpty()) {
        navigate(digest);
    } else {
        verifySource(image, navigate);
    }
}

QJsonObject ForensicsWidget::caseDocument() const
{
    return {
        {"schema", "iaito-forensics-case"},
        {"version", 1},
        {"sources", caseSources},
        {"bookmarks", bookmarks},
        {"events", caseEvents}};
}

bool ForensicsWidget::saveCaseFile(const QString &path, QString *error) const
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    for (const auto &source : sources) {
        if (source->path() == canonical) {
            if (error) {
                *error = tr("Choose a destination outside the evidence sources.");
            }
            return false;
        }
    }
    return writeDocument(path, QJsonDocument(caseDocument()).toJson(), error);
}

bool ForensicsWidget::openCaseFile(const QString &path, QString *error)
{
    QFile file(path);
    if (job || !file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    const auto document = file.size() <= 64 * 1024 * 1024 ? QJsonDocument::fromJson(file.readAll())
                                                          : QJsonDocument();
    const auto data = document.object();
    if (data["schema"].toString() != "iaito-forensics-case" || data["version"].toInt() != 1
        || !data["sources"].isArray() || !data["bookmarks"].isArray()) {
        if (error) {
            *error = tr("This file is not a supported iaito forensic case.");
        }
        return false;
    }
    const QSignalBlocker blockEvidenceSignals(ui->evidence);
    ui->evidence->clear();
    sources.clear();
    sourceIndex = volumeIndex = -1;
    directories.clear();
    volumeProperties.clear();
    ui->verifySource->setEnabled(false);
    ui->allocation->setVolume(nullptr);
    showDirectory();
    caseSources = data["sources"].toArray();
    bookmarks = data["bookmarks"].toArray();
    caseEvents = data["events"].toArray();
    verifiedSourceTimes.clear();
    QStringList missing;
    const auto savedSources = caseSources;
    for (const auto &value : savedSources) {
        const QString image = value.toObject()["path"].toString();
        if (!image.isEmpty() && !addImage(image)) {
            missing.append(image);
        }
    }
    showEvidenceItem(ui->evidence->currentItem());
    showBookmarks();
    if (!missing.isEmpty()) {
        ui->caseStatus->setText(tr("%n evidence sources unavailable", nullptr, int(missing.size())));
        ui->caseStatus->setToolTip(missing.join(QLatin1Char('\n')));
    }
    casePath = path;
    caseDirty = false;
    return true;
}

void ForensicsWidget::openCase()
{
    const QString path = Iaito::getOpenFileName(this, tr("Open Forensic Case"), {}, CaseFilter);
    if (path.isEmpty()) {
        return;
    }
    if (caseDirty
        && QMessageBox::question(
               this,
               tr("Open Forensic Case"),
               tr("Replace the current case with unsaved changes?"),
               QMessageBox::Yes | QMessageBox::No,
               QMessageBox::No)
               != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!openCaseFile(path, &error)) {
        QMessageBox::warning(this, tr("Open Forensic Case"), error);
    }
}

void ForensicsWidget::saveCase()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Save Forensic Case"),
        casePath.isEmpty() ? QStringLiteral("case.iaitocase") : casePath,
        CaseFilter);
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (saveCaseFile(path, &error)) {
        casePath = path;
        caseDirty = false;
        ui->caseStatus->setText(tr("Case saved."));
    } else {
        QMessageBox::warning(this, tr("Save Forensic Case"), error);
    }
}

void ForensicsWidget::exportCase()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Export Forensic Report"),
        QStringLiteral("report.json"),
        tr("JSON (*.json);;CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    const QString canonical = QFileInfo(path).canonicalFilePath();
    for (const auto &source : sources) {
        if (source->path() == canonical) {
            ui->caseStatus->setText(tr("Choose a destination outside the evidence sources."));
            return;
        }
    }
    QByteArray contents;
    if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        const QStringList fields{
            "image",
            "sha256",
            "volumeOffset",
            "entryId",
            "offset",
            "length",
            "name",
            "method",
            "note",
            "created"};
        contents = fields.join(QLatin1Char(',')).toUtf8() + "\r\n";
        for (const auto &value : bookmarks) {
            const auto bookmark = value.toObject();
            QList<QByteArray> row;
            for (const auto &field : fields) {
                row.append(csvField(bookmark.value(field).toString()));
            }
            QByteArray line;
            for (const auto &column : row) {
                if (!line.isEmpty()) {
                    line += ',';
                }
                line += column;
            }
            contents += line + "\r\n";
        }
    } else {
        auto report = caseDocument();
        report["schema"] = "iaito-forensics-report";
        contents = QJsonDocument(report).toJson();
    }
    QString error;
    if (writeDocument(path, contents, &error)) {
        ui->caseStatus->setText(tr("Report exported."));
    } else {
        QMessageBox::warning(this, tr("Export Forensic Report"), error);
    }
}
