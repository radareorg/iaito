#include "dialogs/NewFileDialog.h"
#include "InitialOptionsDialog.h"
#include "common/Configuration.h"
#include "common/FileDialog.h"
#include "common/FilePaths.h"
#include "common/Helpers.h"
#include "common/HighDpiPixmap.h"
#include "common/ResponsiveDialog.h"
#include "common/SamplesDB.h"
#include "core/MainWindow.h"
#include "dialogs/AboutDialog.h"
#ifdef IAITO_ENABLE_DEBUGGER
#include "dialogs/AttachProcDialog.h"
#endif
#include "ui_NewFileDialog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputMethod>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTabBar>
#include <QUrl>
#include <QtGui>

const int NewFileDialog::MaxRecentFiles;

namespace {
// Filters the samples list on the Name (0) and SHA256 (1) columns only, so the
// "On disk" yes/no column never matches the filter text.
class Sha256FilterProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QRegularExpression re = filterRegularExpression();
        if (re.pattern().isEmpty()) {
            return true;
        }
        const QString name = sourceModel()->index(row, 0, parent).data().toString();
        const QString hash = sourceModel()->index(row, 1, parent).data().toString();
        return name.contains(re) || hash.contains(re);
    }
};
} // namespace

static QColor getColorFor(int pos)
{
    static const QList<QColor> colors = {
        QColor(29, 188, 156),
        QColor(52, 152, 219),
        QColor(155, 89, 182),
        QColor(52, 73, 94),
        QColor(231, 76, 60),
        QColor(243, 156, 17),
    };
    return colors[pos % colors.size()];
}

static QColor colorForChar(QChar ch)
{
    static const QList<QColor> palette = {
        QColor(29, 188, 156),
        QColor(52, 152, 219),
        QColor(155, 89, 182),
        QColor(52, 73, 94),
        QColor(231, 76, 60),
        QColor(243, 156, 17),
        QColor(46, 204, 113),
        QColor(241, 196, 15),
        QColor(230, 126, 34),
        QColor(26, 188, 156),
        QColor(142, 68, 173),
        QColor(41, 128, 185),
    };
    const ushort code = ch.toUpper().unicode();
    return palette[code % palette.size()];
}

static QIcon getPlainIcon(const QString &str, int pos)
{
    const int w = 64;
    const int h = 64;

    HighDpiPixmap pixmap(w, h);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setPen(Qt::NoPen);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(getColorFor(pos));
    p.drawEllipse(1, 1, w - 2, h - 2);
    p.setPen(Qt::white);
    QFont font = Config()->getBaseFont();
    font.setBold(true);
    font.setPointSize(18);
    p.setFont(font);
    p.drawText(0, 0, w, h - 2, Qt::AlignCenter, QString(str).toUpper().mid(0, 2));

    return QIcon(pixmap);
}

static QIcon getFancyIcon(const QString &str)
{
    const int w = 64;
    const int h = 64;

    HighDpiPixmap pixmap(w, h);
    pixmap.fill(Qt::transparent);

    QString label = QString(str).toUpper().mid(0, 2);
    if (label.isEmpty()) {
        label = QStringLiteral("??");
    } else if (label.size() == 1) {
        label.append(label);
    }

    const QColor c1 = colorForChar(label.at(0));
    const QColor c2 = colorForChar(label.at(1));

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    const QRectF disc(1.0, 1.0, w - 2.0, h - 2.0);

    QLinearGradient body(0, 0, w, h);
    body.setColorAt(0.0, c1);
    body.setColorAt(1.0, c2);
    p.setBrush(body);
    p.drawEllipse(disc);

    QRadialGradient highlight(w * 0.5, h * 0.22, w * 0.55);
    highlight.setColorAt(0.0, QColor(255, 255, 255, 140));
    highlight.setColorAt(0.55, QColor(255, 255, 255, 40));
    highlight.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(highlight);
    p.drawEllipse(disc);

    QRadialGradient shade(w * 0.5, h * 0.95, w * 0.6);
    shade.setColorAt(0.0, QColor(0, 0, 0, 110));
    shade.setColorAt(0.5, QColor(0, 0, 0, 45));
    shade.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.setBrush(shade);
    p.drawEllipse(disc);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 90), 1.2));
    p.drawEllipse(disc);
    p.setPen(QPen(QColor(255, 255, 255, 70), 1.0));
    p.drawEllipse(disc.adjusted(1.5, 1.5, -1.5, -1.5));

    QFont font = Config()->getBaseFont();
    font.setBold(true);
    font.setPointSize(18);
    p.setFont(font);

    const QRect textRect(0, 0, w, h - 2);
    p.setPen(QColor(0, 0, 0, 140));
    p.drawText(textRect.adjusted(0, 2, 0, 2), Qt::AlignCenter, label);
    p.setPen(Qt::white);
    p.drawText(textRect, Qt::AlignCenter, label);

    return QIcon(pixmap);
}

static bool isNewFormatProject(const QString &project)
{
    QString dir = Config()->getDirProjects();
    if (dir.startsWith(QLatin1Char('~'))) {
        dir = QDir::homePath() + dir.mid(1);
    }
    return QFileInfo(QDir(dir).filePath(project)).isDir();
}

NewFileDialog::NewFileDialog(MainWindow *main)
    : QDialog(nullptr)
    , // no parent on purpose, using main causes weird positioning
    ui(new Ui::NewFileDialog)
    , main(main)
{
    ui->setupUi(this);
    mobilePage = ResponsiveDialog::prepareMobilePage(this);
    if (mobilePage) {
        setupMobilePage();
    }
    for (auto *page :
         {ui->filesTab, ui->shellcodeTab, ui->projectsTab, ui->attachTab, ui->sha256Tab}) {
        ResponsiveDialog::makeScrollable(
            page, page->findChild<QWidget *>(page->objectName() + "Actions"));
    }
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
    setAcceptDrops(true);
    applyListFonts();
    ui->recentsListWidget->addAction(ui->actionRemove_item);
    ui->recentsListWidget->addAction(ui->actionClear_all);
    ui->projectsListWidget->addAction(ui->actionRemove_project);
    ui->logoSvgWidget->load(Config()->getLogoFile());

    setSpacerEnabled(ui->verticalSpacer, false);
    // radare2 does not seem to save this config so here we load this manually
    Core()->setConfig("dir.projects", Config()->getDirProjects());

    fillRecentFilesList();
    fillIOPluginsList();
    fillProjectsList();
#ifdef IAITO_ENABLE_DEBUGGER
    setupAttachTab();
#else
    {
        int attachTabIdx = ui->tabWidget->indexOf(ui->attachTab);
        if (attachTabIdx >= 0) {
            ui->tabWidget->removeTab(attachTabIdx);
        }
    }
#endif

    ui->logoSvgWidget->setToolTip(tr("About Iaito"));

    // Set last clicked tab
    ui->tabWidget->setCurrentIndex(Config()->getNewFileLastClicked());

    ui->loadProjectButton->setEnabled(ui->projectsListWidget->currentItem() != nullptr);
    ui->exportProjectButton->setEnabled(ui->projectsListWidget->currentItem() != nullptr);

    if (!mobilePage) {
        qhelpers::attachFilePathCompleter(ui->newFileEdit);
    }
    connect(ui->ioPlugin, &QComboBox::currentTextChanged, this, [this]() {
        on_newFileEdit_textChanged(ui->newFileEdit->text());
    });

    // A touch launch should show the file browser, not immediately summon the keyboard.
    if (mobilePage) {
        for (auto *button : findChildren<QPushButton *>()) {
            button->setAutoDefault(false);
            button->setDefault(false);
        }
        ui->selectFileButton->setFocus();
        connect(ui->newFileEdit, &QLineEdit::returnPressed, this, [this]() {
            auto *next = ui->loadFileButton->isEnabled() ? ui->loadFileButton
                                                         : ui->selectFileButton;
            next->setFocus();
            qApp->inputMethod()->hide();
        });
        connect(this, &QDialog::finished, this, []() { qApp->inputMethod()->hide(); });
    } else {
        ui->newFileEdit->setFocus();
    }
    on_newFileEdit_textChanged(ui->newFileEdit->text());
    on_shellcodeText_textChanged();
}

NewFileDialog::~NewFileDialog() {}

void NewFileDialog::resizeEvent(QResizeEvent *event)
{
    if (!mobilePage) {
        ui->logoSvgWidget->setVisible(width() >= 600 && height() >= 500);
    }
    QDialog::resizeEvent(event);
}

void NewFileDialog::keyPressEvent(QKeyEvent *event)
{
    // The startup page is the only visible window until a file is opened.
    if (mobilePage && !main->isVisible() && event->key() == Qt::Key_Escape) {
        qApp->inputMethod()->hide();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void NewFileDialog::setupMobilePage()
{
    ui->verticalLayout->setContentsMargins(20, 16, 20, 16);
    ui->verticalLayout->setSpacing(16);
    ui->verticalLayout_6->removeWidget(ui->logoSvgWidget);
    auto *header = new QHBoxLayout;
    ui->logoSvgWidget->setFixedSize(40, 40);
    header->addWidget(ui->logoSvgWidget);
    auto *titles = new QVBoxLayout;
    auto *title = new QLabel(tr("iaito"), this);
    QFont font = title->font();
    font.setPointSize(24);
    font.setBold(true);
    title->setFont(font);
    titles->addWidget(title);
    auto *subtitle = new QLabel(tr("Open a binary or continue a project"), this);
    subtitle->setWordWrap(true);
    titles->addWidget(subtitle);
    header->addLayout(titles, 1);
    if (main->isVisible()) {
        auto *cancel = new QPushButton(tr("Cancel"), this);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        header->addWidget(cancel);
    }
    ui->verticalLayout_6->addLayout(header);
    ui->tabWidget->setStyleSheet(QString());
    ui->tabWidget->tabBar()->setExpanding(true);
    ui->tabWidget->setUsesScrollButtons(true);

    // Keep the primary Files action prominent. The URI selector and manual path
    // remain available on a separate row without squeezing the text field.
    auto *grid = ui->gridLayout;
    grid->removeWidget(ui->selectFileButton);
    grid->removeWidget(ui->ioPlugin);
    grid->removeWidget(ui->newFileEdit);
    ui->selectFileButton->setText(tr("Browse Files…"));
    ui->selectFileButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    grid->addWidget(ui->selectFileButton, 0, 0, 1, 2);
    auto *pathLabel = new QLabel(tr("File path or URI"), ui->filesTab);
    pathLabel->setBuddy(ui->newFileEdit);
    grid->addWidget(pathLabel, 1, 0, 1, 2);
    ui->newFileEdit->setFrame(true);
    ui->newFileEdit->setPlaceholderText(tr("Enter a path or URI"));
    ui->newFileEdit->setInputMethodHints(Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText);
    ui->ioPlugin->setAccessibleName(tr("URI handler"));
    ui->ioPlugin->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ui->ioPlugin->setMinimumContentsLength(8);
    grid->addWidget(ui->ioPlugin, 2, 0);
    grid->addWidget(ui->newFileEdit, 2, 1);
    grid->setColumnStretch(1, 1);
    grid->setVerticalSpacing(10);
    auto *recents = new QLabel(ui->filesTab);
    recents->setWordWrap(true);
    auto updateRecents = [this, recents]() {
        recents->setText(
            ui->recentsListWidget->count()
                ? tr("Recent files")
                : tr("Recent files will appear here after you open a binary."));
    };
    ui->verticalLayout_4->insertWidget(1, recents);
    connect(ui->recentsListWidget->model(), &QAbstractItemModel::rowsInserted, recents, updateRecents);
    connect(ui->recentsListWidget->model(), &QAbstractItemModel::rowsRemoved, recents, updateRecents);
    connect(ui->recentsListWidget->model(), &QAbstractItemModel::modelReset, recents, updateRecents);
    updateRecents();
    ui->loadFileButton->setText(tr("Open"));
}

void NewFileDialog::on_loadFileButton_clicked()
{
    loadFile(ui->newFileEdit->text());
}

void NewFileDialog::on_checkBox_FilelessOpen_clicked()
{
    /*
     * When we're not opening any file, we must hide all file-related widgets
     */
    bool disable_and_hide = !ui->checkBox_FilelessOpen->isChecked();
    setSpacerEnabled(ui->verticalSpacer, !disable_and_hide);
    QVector<QWidget *> widgets_to_hide
        = {ui->recentsListWidget, ui->ioPlugin, ui->newFileEdit, ui->selectFileButton};
    for (QWidget *widget : widgets_to_hide) {
        setDisableAndHideWidget(widget, disable_and_hide);
    }
    on_newFileEdit_textChanged(ui->newFileEdit->text());
}

void NewFileDialog::on_selectFileButton_clicked()
{
    const QString currentDir = Config()->getRecentFolder();
    const QUrl initial = currentDir.contains(QStringLiteral("://"))
                             ? QUrl(currentDir)
                             : QUrl::fromLocalFile(currentDir);
    const QString fileName = Iaito::getOpenFileName(this, tr("Select file"), initial);
    if (mobilePage) {
        ui->selectFileButton->setFocus();
    }

    if (!fileName.isEmpty()) {
        ui->newFileEdit->setText(fileName);
        ui->ioPlugin->setCurrentText(Iaito::fileUriScheme(fileName));
        ui->loadFileButton->setFocus();
        Config()->setRecentFolder(
            fileName.contains(QStringLiteral("://")) ? fileName
                                                     : QFileInfo(fileName).absolutePath());
    }
}

void NewFileDialog::on_newFileEdit_textChanged(const QString &text)
{
    const QString trimmed = text.trimmed();
    const bool fileless = ui->checkBox_FilelessOpen->isChecked();
    const bool isUri = trimmed.contains(QStringLiteral("://"));
    if (isUri) {
        const QSignalBlocker blocker(ui->ioPlugin);
        ui->ioPlugin->setCurrentText(Iaito::fileUriScheme(trimmed));
    }
    const bool useFilePlugin = ui->ioPlugin->currentText() == QStringLiteral("file://") && !isUri;

    bool pathExists = true;
    if (useFilePlugin && !trimmed.isEmpty()) {
        QString path = trimmed;
        if (path.startsWith(QLatin1Char('~'))) {
            path = QDir::homePath() + path.mid(1);
        }
        pathExists = QFileInfo::exists(path);
    }

    bool enable = fileless
                  || (!trimmed.isEmpty() && !trimmed.endsWith(QStringLiteral("://"))
                      && (!useFilePlugin || pathExists));
    ui->loadFileButton->setEnabled(enable);
}

void NewFileDialog::on_selectProjectsDirButton_clicked()
{
    auto currentDir = Config()->getDirProjects();

    if (currentDir.startsWith("~")) {
        currentDir = QDir::homePath() + currentDir.mid(1);
    }
    const QString &dir = QDir::toNativeSeparators(
        QFileDialog::getExistingDirectory(this, tr("Select project path (dir.projects)"), currentDir));

    if (dir.isEmpty()) {
        return;
    }
    if (!QFileInfo(dir).isWritable()) {
        QMessageBox::critical(
            this, tr("Permission denied"), tr("You do not have write access to <b>%1</b>").arg(dir));
        return;
    }

    Config()->setDirProjects(dir);
    Core()->setConfig("dir.projects", dir);
    fillProjectsList();
}

void NewFileDialog::on_loadProjectButton_clicked()
{
    QListWidgetItem *item = ui->projectsListWidget->currentItem();

    if (item == nullptr) {
        return;
    }

    loadProject(item->data(Qt::UserRole).toString());
}

void NewFileDialog::on_shellcodeButton_clicked()
{
    QString shellcode = ui->shellcodeText->toPlainText();
    QString extractedCode = "";
    static const QRegularExpression rx("([0-9a-f]{2})", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator i = rx.globalMatch(shellcode);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        extractedCode.append(match.captured(1));
    }
    int size = extractedCode.size() / 2;
    if (size > 0) {
        loadShellcode(extractedCode, size);
    } else {
        QMessageBox::warning(this, tr("Error"), tr("Invalid hexpair string"));
    }
}

void NewFileDialog::on_shellcodeText_textChanged()
{
    const QString shellcode = ui->shellcodeText->toPlainText();
    int count = 0;
    static const QRegularExpression rx("([0-9a-f]{2})", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = rx.globalMatch(shellcode);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    ui->shellcodeButton->setEnabled(count > 0);
}

void NewFileDialog::on_recentsListWidget_itemClicked(QListWidgetItem *item)
{
    QVariant data = item->data(Qt::UserRole);
    QString sitem = data.toString();
    ui->newFileEdit->setText(sitem);
    ui->ioPlugin->setCurrentText(Iaito::fileUriScheme(sitem));
}

void NewFileDialog::on_recentsListWidget_itemDoubleClicked(QListWidgetItem *item)
{
    loadFile(item->data(Qt::UserRole).toString());
}

void NewFileDialog::on_projectsListWidget_itemSelectionChanged()
{
    bool hasSelection = ui->projectsListWidget->currentItem() != nullptr;
    ui->loadProjectButton->setEnabled(hasSelection);
    ui->exportProjectButton->setEnabled(hasSelection);
}

void NewFileDialog::on_projectsListWidget_itemDoubleClicked(QListWidgetItem *item)
{
    loadProject(item->data(Qt::UserRole).toString());
}

void NewFileDialog::on_logoSvgWidget_clicked()
{
    AboutDialog *a = new AboutDialog(this);
    a->setAttribute(Qt::WA_DeleteOnClose);
    a->open();
}

void NewFileDialog::on_actionRemove_item_triggered()
{
    // Remove selected item from recents list
    QListWidgetItem *item = ui->recentsListWidget->currentItem();

    if (item == nullptr)
        return;

    QVariant data = item->data(Qt::UserRole);
    QString sitem = data.toString();

    QSettings settings;
    QStringList files = settings.value("recentFileList").toStringList();
    files.removeAll(sitem);
    settings.setValue("recentFileList", files);

    ui->recentsListWidget->takeItem(ui->recentsListWidget->currentRow());

    ui->newFileEdit->clear();
}

void NewFileDialog::on_actionClear_all_triggered()
{
    // Clear recent file list
    QSettings settings;
    QStringList files = settings.value("recentFileList").toStringList();
    files.clear();

    ui->recentsListWidget->clear();
    // TODO: if called from main window its ok, otherwise its not
    settings.setValue("recentFileList", files);
    ui->newFileEdit->clear();
}

void NewFileDialog::on_actionRemove_project_triggered()
{
    IaitoCore *core = Core();

    QListWidgetItem *item = ui->projectsListWidget->currentItem();

    if (item == nullptr)
        return;

    QVariant data = item->data(Qt::UserRole);
    QString sitem = data.toString();

    // Confirmation box
    QMessageBox msgBox(this);
    msgBox.setText(tr("Delete the project \"%1\" from disk ?").arg(sitem));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    int ret = msgBox.exec();

    switch (ret) {
    case QMessageBox::Yes:
        core->deleteProject(sitem);
        ui->projectsListWidget->takeItem(ui->projectsListWidget->currentRow());
        break;
    case QMessageBox::No:
    default:
        break;
    }
}

void NewFileDialog::on_importProjectButton_clicked()
{
    QString currentDir = Config()->getDirProjects();
    if (currentDir.startsWith("~")) {
        currentDir = QDir::homePath() + currentDir.mid(1);
    }
    QString zipFile = QDir::toNativeSeparators(
        QFileDialog::getOpenFileName(
            this,
            tr("Import project from zip"),
            currentDir,
            tr("Zip files (*.zip)"),
            nullptr,
            QFILEDIALOG_FLAGS));
    if (zipFile.isEmpty()) {
        return;
    }
    Core()->cmdRaw(QStringLiteral("Pzi %1").arg(zipFile));
    fillProjectsList();
}

void NewFileDialog::on_exportProjectButton_clicked()
{
    QListWidgetItem *item = ui->projectsListWidget->currentItem();
    if (item == nullptr) {
        return;
    }
    QString project = item->data(Qt::UserRole).toString();
    QString currentDir = Config()->getDirProjects();
    if (currentDir.startsWith("~")) {
        currentDir = QDir::homePath() + currentDir.mid(1);
    }
    QString defaultPath = QDir(currentDir).filePath(project + ".zip");
    QString zipFile = QDir::toNativeSeparators(
        QFileDialog::getSaveFileName(
            this,
            tr("Export project to zip"),
            defaultPath,
            tr("Zip files (*.zip)"),
            nullptr,
            QFILEDIALOG_FLAGS));
    if (zipFile.isEmpty()) {
        return;
    }
    Core()->cmdRaw(QStringLiteral("Pze %1 %2").arg(project).arg(zipFile));
}

void NewFileDialog::applyListFonts()
{
    const QFont font = QApplication::font();
    const auto views = findChildren<QAbstractItemView *>();
    for (QAbstractItemView *view : views) {
        view->setFont(font);
    }
}

void NewFileDialog::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept drag & drop events only if they provide a URL
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void NewFileDialog::dropEvent(QDropEvent *event)
{
    // Accept drag & drop events only if they provide a URL
    if (event->mimeData()->urls().count() == 0) {
        qWarning() << "No URL in drop event, ignoring it.";
        return;
    }

    event->acceptProposedAction();
    const QString fileName = Iaito::fileNameFromUrl(event->mimeData()->urls().first());
    ui->newFileEdit->setText(fileName);
    ui->ioPlugin->setCurrentText(Iaito::fileUriScheme(fileName));
    loadFile(fileName);
}

bool NewFileDialog::fillRecentFilesList()
{
    // Fill list with recent opened files
    QSettings settings;

    QStringList files = settings.value("recentFileList").toStringList();

    QMutableListIterator<QString> it(files);
    int i = 0;
    while (it.hasNext()) {
        // Get the file name
        const QString fullpath = Iaito::fileNameForIO(it.next(), QStringLiteral("file://"));
        if (fullpath.contains(QStringLiteral("://"))) {
            // Providers are not filesystem paths. Keep the grant-backed URI for reopening.
            const QString name = QUrl(fullpath).fileName();
            auto *item = new QListWidgetItem(getPlainIcon(name, i++), name + '\n' + fullpath);
            item->setData(Qt::UserRole, fullpath);
            ui->recentsListWidget->addItem(item);
            continue;
        }
        const QString homepath = QDir::homePath();
        const QFileInfo info(fullpath);
        const QString basename = info.fileName();
        QString filenameHome = info.absolutePath();
        if (filenameHome.startsWith(homepath)) {
            filenameHome.replace(0, homepath.length(), QStringLiteral("~"));
        }
        if (!info.exists()) {
            it.remove();
        } else {
            // Format the text and add the item to the file list
            const QString text
                = QStringLiteral("%1\n%2\nSize: %3")
                      .arg(basename, filenameHome, qhelpers::formatBytecount(info.size()));
            QListWidgetItem *item = new QListWidgetItem(getPlainIcon(basename, i), text);
            item->setData(Qt::UserRole, fullpath);
            ui->recentsListWidget->addItem(item);
            i++;
        }
    }

    // Removed files were deleted from the stringlist. Save it again.
    settings.setValue("recentFileList", files);

    return !files.isEmpty();
}

bool NewFileDialog::fillProjectsList()
{
    IaitoCore *core = Core();

    auto currentDir = Config()->getDirProjects();

    ui->projectsDirEdit->setText(currentDir);

    QStringList projects = core->getProjectNames();
    projects.sort(Qt::CaseInsensitive);

    ui->projectsListWidget->clear();

    int i = 0;
    for (const QString &project : projects) {
        QString text = project;
        QJsonObject info = core->cmdj(QString("'Pij %1").arg(project).toUtf8().constData()).object();
        if (!info.isEmpty()) {
            QString file = info["file"].toString();
            QString modified = info["modified"].toString();
            text = QStringLiteral("%1\n%2\n%3")
                       .arg(
                           project,
                           file.isEmpty() ? "(no file)" : file,
                           modified.isEmpty() ? "" : modified);
        }
        QIcon icon = isNewFormatProject(project) ? getFancyIcon(project) : getPlainIcon(project, i);
        QListWidgetItem *item = new QListWidgetItem(icon, text);

        item->setData(Qt::UserRole, project);
        ui->projectsListWidget->addItem(item);
        i++;
    }

    return !projects.isEmpty();
}

void NewFileDialog::fillIOPluginsList()
{
    ui->ioPlugin->clear();
    ui->ioPlugin->addItem("file://");
    ui->ioPlugin->setItemData(0, tr("Open a file with no extra treatment."), Qt::ToolTipRole);
    int index = 1;
    QList<RIOPluginDescription> ioPlugins = Core()->getRIOPluginDescriptions();
    for (int i = 0; i < ioPlugins.length(); i++) {
        RIOPluginDescription plugin = ioPlugins.at(i);
        //// Hide debug plugins?
        // if (plugin.permissions.contains('d')) {
        //     continue;
        // }
        const auto &uris = plugin.uris;
        for (const auto &uri : uris) {
            if (uri == "file://") {
                continue;
            }
            ui->ioPlugin->addItem(uri);
            ui->ioPlugin->setItemData(index, plugin.description, Qt::ToolTipRole);
            index++;
        }
    }
    ui->ioPlugin->model()->sort(0);
#ifdef Q_OS_ANDROID
    ui->ioPlugin->setCurrentText(QStringLiteral("content://"));
#else
    ui->ioPlugin->setCurrentText(QStringLiteral("file://"));
#endif

    QFontMetrics fm = ui->ioPlugin->view()->fontMetrics();
    int maxWidth = 0;
    for (int i = 0; i < ui->ioPlugin->count(); i++) {
        maxWidth = qMax(maxWidth, fm.horizontalAdvance(ui->ioPlugin->itemText(i)));
    }
    ui->ioPlugin->view()->setMinimumWidth(maxWidth + 24);
}

void NewFileDialog::loadFile(const QString &filename)
{
    bool skipOpeningFile = ui->checkBox_FilelessOpen->isChecked();
    if ((filename == "" || filename.endsWith("://")) && !skipOpeningFile) {
        QMessageBox::warning(this, tr("Error"), tr("Select a file before clicking this button"));
        return;
    }

    if (skipOpeningFile) {
        /* If we don't want to open a file just skip setting options and move to
         * finalize */
        main->finalizeOpen();
        close();
        return;
    }

    const QString ioFile = Iaito::fileNameForIO(filename, ui->ioPlugin->currentText());
    if (!ioFile.contains(QStringLiteral("://")) && !Core()->tryFile(ioFile, false)) {
        QMessageBox::warning(
            this, tr("Error"), tr("Select a new program or a previous one before continuing."));
        return;
    }

    // Add file to recent file list
    QSettings settings;
    QStringList files = settings.value("recentFileList").toStringList();
    files.removeAll(ioFile);
    files.prepend(ioFile);
    while (files.size() > MaxRecentFiles)
        files.removeLast();

    settings.setValue("recentFileList", files);

    // Close dialog and open MainWindow/InitialOptionsDialog
    InitialOptions options;
    options.filename = ioFile;
    main->openNewFile(options);

    close();
}

void NewFileDialog::loadProject(const QString &project)
{
    MainWindow *main = new MainWindow();
    main->openProject(project);

    close();
}

void NewFileDialog::loadShellcode(const QString &shellcode, const int size)
{
    MainWindow *main = new MainWindow();
    InitialOptions options;
    options.filename = QStringLiteral("malloc://%1").arg(size);
    options.shellcode = shellcode;
    // Set base address from user input (if provided)
    {
        QString baseAddrStr = ui->baseAddressLineEdit->text().trimmed();
        if (!baseAddrStr.isEmpty()) {
            ut64 addr = Core()->math(baseAddrStr);
            options.binLoadAddr = addr;
            options.mapAddr = addr;
        }
    }
    main->openNewFile(options);
    close();
}

void NewFileDialog::setDisableAndHideWidget(QWidget *w, bool disable_and_hide)
{
    // w->setDisabled(disable_and_hide);
    w->setVisible(disable_and_hide);
}

void NewFileDialog::setSpacerEnabled(QSpacerItem *s, bool enabled, int w, int h)
{
    if (enabled) {
        s->changeSize(w, h, QSizePolicy::Expanding, QSizePolicy::Expanding);
    } else {
        s->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
}

void NewFileDialog::initSha256Model()
{
    if (sha256Model) {
        return;
    }
    sha256Model = new QStandardItemModel(this);
    sha256Model->setHorizontalHeaderLabels({tr("Name"), tr("SHA256"), tr("On disk")});
    sha256Proxy = new Sha256FilterProxy(this);
    sha256Proxy->setSourceModel(sha256Model);
    sha256Proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->sha256TableView->setModel(sha256Proxy);
    ui->sha256TableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->sha256TableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->sha256TableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->sha256TableView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->sha256TableView->verticalHeader()->hide();
    ui->sha256TableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->sha256TableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->sha256TableView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    connect(
        ui->sha256FilterEdit,
        &QLineEdit::textChanged,
        sha256Proxy,
        &QSortFilterProxyModel::setFilterFixedString);

    refreshSha256Model();
}

void NewFileDialog::refreshSha256Model()
{
    if (!sha256Model) {
        return;
    }
    sha256Model->removeRows(0, sha256Model->rowCount());
    const auto entries = SamplesDB::all();
    for (const auto &e : entries) {
        const QString &hash = e.first;
        const QString &path = e.second;
        auto *name = new QStandardItem(QFileInfo(path).fileName());
        name->setData(path, Qt::UserRole);
        name->setToolTip(path);
        auto *hashItem = new QStandardItem(hash);
        auto *existsItem = new QStandardItem(QFileInfo::exists(path) ? tr("yes") : tr("no"));
        sha256Model->appendRow({name, hashItem, existsItem});
    }
}

QString NewFileDialog::selectedSha256() const
{
    if (!sha256Proxy) {
        return QString();
    }
    const QModelIndex idx = ui->sha256TableView->currentIndex();
    if (!idx.isValid()) {
        return QString();
    }
    const QModelIndex src = sha256Proxy->mapToSource(idx);
    return sha256Model->item(src.row(), 1)->text();
}

QString NewFileDialog::selectedSha256Path() const
{
    if (!sha256Proxy) {
        return QString();
    }
    const QModelIndex idx = ui->sha256TableView->currentIndex();
    if (!idx.isValid()) {
        return QString();
    }
    const QModelIndex src = sha256Proxy->mapToSource(idx);
    return sha256Model->item(src.row(), 0)->data(Qt::UserRole).toString();
}

void NewFileDialog::openSamplePath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Open sample"), tr("File no longer exists:\n%1").arg(path));
        return;
    }
    InitialOptions options;
    options.filename = QDir::toNativeSeparators(path);
    main->openNewFile(options);
    close();
}

void NewFileDialog::on_sha256OpenButton_clicked()
{
    openSamplePath(selectedSha256Path());
}

void NewFileDialog::on_sha256TableView_doubleClicked(const QModelIndex &index)
{
    if (!index.isValid() || !sha256Proxy) {
        return;
    }
    const QModelIndex src = sha256Proxy->mapToSource(index);
    openSamplePath(sha256Model->item(src.row(), 0)->data(Qt::UserRole).toString());
}

void NewFileDialog::on_sha256DeleteButton_clicked()
{
    const QString hash = selectedSha256();
    if (hash.isEmpty()) {
        return;
    }
    SamplesDB::remove(hash);
    refreshSha256Model();
}

void NewFileDialog::on_sha256LookupButton_clicked()
{
    const QString hash = selectedSha256();
    if (hash.isEmpty()) {
        return;
    }
    QDesktopServices::openUrl(QUrl(Config()->getSha256LookupBaseUrl() + hash, QUrl::TolerantMode));
}

void NewFileDialog::on_sha256ScanButton_clicked()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select folder to scan"), Config()->getRecentFolder());
    if (dir.isEmpty()) {
        return;
    }
    initSha256Model();
    ui->sha256ScanButton->setEnabled(false);
    ui->sha256ScanButton->setText(tr("Scanning..."));
    QPointer<NewFileDialog> self(this);
    SamplesDB::scanFolderAsync(dir, [self]() {
        QMetaObject::invokeMethod(qApp, [self]() {
            if (!self) {
                return;
            }
            self->refreshSha256Model();
            self->ui->sha256ScanButton->setText(QObject::tr("Scan folder..."));
            self->ui->sha256ScanButton->setEnabled(true);
        });
    });
}

void NewFileDialog::on_sha256TableView_customContextMenuRequested(const QPoint &pos)
{
    if (!sha256Proxy) {
        return;
    }
    const QModelIndex idx = ui->sha256TableView->indexAt(pos);
    if (!idx.isValid()) {
        return;
    }
    const QModelIndex src = sha256Proxy->mapToSource(idx);
    const QString path = sha256Model->item(src.row(), 0)->data(Qt::UserRole).toString();
    const QString hash = sha256Model->item(src.row(), 1)->text();

    QMenu menu(this);
    menu.addAction(tr("Open"), this, [this, path]() { openSamplePath(path); });
    menu.addAction(tr("Open in lookup URL"), this, [hash]() {
        QDesktopServices::openUrl(
            QUrl(Config()->getSha256LookupBaseUrl() + hash, QUrl::TolerantMode));
    });
    menu.addSeparator();
    menu.addAction(tr("Copy path"), this, [path]() { QApplication::clipboard()->setText(path); });
    menu.addAction(tr("Copy hash"), this, [hash]() { QApplication::clipboard()->setText(hash); });
    menu.addAction(tr("Open containing folder"), this, [path]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    menu.addSeparator();
    menu.addAction(tr("Remove from database"), this, [this, hash]() {
        SamplesDB::remove(hash);
        refreshSha256Model();
    });
    menu.exec(ui->sha256TableView->viewport()->mapToGlobal(pos));
}

void NewFileDialog::on_tabWidget_currentChanged(int index)
{
    Config()->setNewFileLastClicked(index);
    if (index == ui->tabWidget->indexOf(ui->sha256Tab) && !sha256TabPopulated) {
        sha256TabPopulated = true;
        initSha256Model();
    }
#ifdef IAITO_ENABLE_DEBUGGER
    if (index == ui->tabWidget->indexOf(ui->attachTab) && !attachTabPopulated) {
        attachTabPopulated = true;
        initAttachModel();
    }
#endif
}

#ifdef IAITO_ENABLE_DEBUGGER
void NewFileDialog::setupAttachTab()
{
    // Model creation is deferred to on_tabWidget_currentChanged to avoid
    // querying the process list until the Attach tab is actually opened.
}

void NewFileDialog::initAttachModel()
{
    if (processModel) {
        return;
    }

    processModel = new ProcessModel(this);
    processProxyModel = new ProcessProxyModel(processModel, this);

    ui->attachProcView->setModel(processProxyModel);
    ui->attachProcView->sortByColumn(ProcessModel::PidColumn, Qt::DescendingOrder);
    ui->attachProcView->header()
        ->setSectionResizeMode(ProcessModel::PidColumn, QHeaderView::ResizeToContents);
    ui->attachProcView->header()
        ->setSectionResizeMode(ProcessModel::UidColumn, QHeaderView::ResizeToContents);
    ui->attachProcView->header()
        ->setSectionResizeMode(ProcessModel::StatusColumn, QHeaderView::ResizeToContents);
    ui->attachProcView->header()
        ->setSectionResizeMode(ProcessModel::PathColumn, QHeaderView::Stretch);

    connect(
        ui->attachFilterEdit,
        &QLineEdit::textChanged,
        processProxyModel,
        &QSortFilterProxyModel::setFilterWildcard);

    connect(
        ui->attachProcView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
            ui->attachButton->setEnabled(ui->attachProcView->selectionModel()->hasSelection());
        });
}

void NewFileDialog::on_refreshProcessesButton_clicked()
{
    initAttachModel();
    auto *view = ui->attachProcView;
    bool hadSelection = view->selectionModel()->hasSelection();
    int prevScrollPos = 0;
    int prevPID = 0;

    if (hadSelection) {
        prevScrollPos = view->verticalScrollBar()->value();
        prevPID = view->selectionModel()
                      ->currentIndex()
                      .data(ProcessModel::ProcDescriptionRole)
                      .value<ProcessDescription>()
                      .pid;
    }

    processModel->updateData();

    if (hadSelection) {
        QModelIndexList idx = view->model()->match(
            view->model()->index(0, 0), Qt::DisplayRole, QVariant::fromValue(prevPID));
        if (!idx.isEmpty()) {
            view->setCurrentIndex(idx.first());
            view->verticalScrollBar()->setValue(prevScrollPos);
        }
    }
}

void NewFileDialog::on_attachButton_clicked()
{
    if (!ui->attachProcView->selectionModel()->hasSelection()) {
        return;
    }
    int pid = ui->attachProcView->selectionModel()
                  ->currentIndex()
                  .data(ProcessModel::ProcDescriptionRole)
                  .value<ProcessDescription>()
                  .pid;
    attachProcess(pid);
}

void NewFileDialog::on_attachProcView_doubleClicked(const QModelIndex &index)
{
    Q_UNUSED(index);
    on_attachButton_clicked();
}

void NewFileDialog::attachProcess(int pid)
{
    if (pid <= 0) {
        QMessageBox::warning(this, tr("Error"), tr("Invalid process selected"));
        return;
    }

    InitialOptions options;
    options.filename = QStringLiteral("dbg://%1").arg(pid);
    options.debug = true;

    QList<CommandDescription> cmds;
    if (ui->interruptCheckBox->isChecked()) {
        cmds.append(CommandDescription{"dk 2", "Interrupt process"});
    }
    options.analCmd = cmds;

    main->openNewFile(options);
    close();
}
#endif // IAITO_ENABLE_DEBUGGER
