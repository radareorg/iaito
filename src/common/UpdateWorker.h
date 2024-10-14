#ifndef UPDATEWORKER_H
#define UPDATEWORKER_H

#include <QtGlobal>

#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
#define IAITO_UPDATE_WORKER_AVAILABLE 1
#else
#define IAITO_UPDATE_WORKER_AVAILABLE 0
#endif

#if IAITO_UPDATE_WORKER_AVAILABLE

#include <QDir>
#include <QObject>
#include <QTimer>
#include <QtNetwork/QNetworkAccessManager>

#include <QVersionNumber>

class QNetworkReply;

/**
 * @class UpdateWorker
 * @brief The UpdateWorker class is a class providing API to check for current
 * Iaito version and download specific version of one.
 */

class UpdateWorker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateWorker(QObject *parent = nullptr);

    /**
     * @fn void UpdateWorker::checkCurrentVersion(time_t timeoutMs)
     *
     * Sends request to determine current version of Iaito.
     * If there is no response in @a timeoutMs milliseconds, emits
     * @fn UpdateWorker::checkComplete(const QString& currVerson, const QString&
     * errorMsg) with timeout error message.
     *
     *
     * @sa checkComplete(const QString& verson, const QString& errorMsg)
     */

    void checkCurrentVersion(time_t timeoutMs);

    /**
     * @fn void UpdateWorker::download(QDir downloadPath, QString version)
     *
     * @brief Downloads provided @a version of Iaito into @a downloadDir.
     *
     * @sa downloadProcess(size_t bytesReceived, size_t bytesTotal)
     */
    void download(QString filename, QString version);

    /**
     * @fn void UpdateWorker::showUpdateDialog()
     *
     * Shows dialog that allows user to either download latest version of Iaito
     * from website or download it by clicking on a button. This dialog also has
     * "Don't check for updates" button which disables on-start update checks if
     * @a showDontCheckForUpdatesButton is true.
     *
     * @sa downloadProcess(size_t bytesReceived, size_t bytesTotal)
     */
    void showUpdateDialog(bool showDontCheckForUpdatesButton);

    /**
     * @return the version of this Iaito binary, derived from
     * IAITO_VERSION_MAJOR, IAITO_VERSION_MINOR and IAITO_VERSION_PATCH.
     */
    static QVersionNumber currentVersionNumber();

public slots:
    /**
     * @fn void UpdateWorker::abortDownload()
     *
     * @brief Stops current process of downloading.
     *
     * @note UpdateWorker::downloadFinished(QString filename) is not send after
     * this function.
     *
     * @sa download(QDir downloadDir, QString version)
     */
    void abortDownload();

signals:
    /**
     * @fn UpdateWorker::checkComplete(const QString& verson, const QString&
     * errorMsg)
     *
     * The signal is emitted when check has been done with an empty @a errorMsg
     * string. In case of an error @a currVerson is null and @a errorMsg
     * contains description of error.
     */
    void checkComplete(const QVersionNumber &currVerson, const QString &errorMsg);

    /**
     * @fn UpdateWorker::downloadProcess(size_t bytesReceived, size_t
     * bytesTotal)
     *
     * The signal is emitted each time when some amount of bytes was downloaded.
     * May be used as indicator of download progress.
     */
    void downloadProcess(size_t bytesReceived, size_t bytesTotal);

    /**
     * @fn UpdateWorker::downloadFinished(QString filename)
     *
     * @brief The signal is emitted as soon as downloading completes.
     */
    void downloadFinished(QString filename);

    /**
     * @fn UpdateWorker::downloadError(QString errorStr)
     *
     * @brief The signal is emitted when error occures during download.
     */
    void downloadError(QString errorStr);

private slots:
    void serveVersionCheckReply();

    void serveDownloadFinish();

    void process(size_t bytesReceived, size_t bytesTotal);

private:
    QString getRepositeryExt() const;
    QString getRepositoryFileName() const;

private:
    QNetworkAccessManager nm;
    QVersionNumber latestVersion;
    QTimer t;
    bool pending;
    QFile downloadFile;
    QNetworkReply *downloadReply;
    QNetworkReply *checkReply;
};

#endif // IAITO_UPDATE_WORKER_AVAILABLE
#endif // UPDATEWORKER_H
