#ifndef RECOVERYENGINE_H
#define RECOVERYENGINE_H

#include <QObject>
#include <QProcess>
#include <QElapsedTimer>
#include <QTimer>
#include "statemanager.h"
#include "logmodel.h"

class RecoveryEngine : public QObject {
    Q_OBJECT
public:
    enum State { Idle, Running, Pausing, Paused, Completed, Error };
    Q_ENUM(State)

    explicit RecoveryEngine(LogModel *logModel, QObject *parent = nullptr);
    ~RecoveryEngine();

    State state() const { return m_state; }
    RecoveryState &recoveryState() { return m_stateData; }
    const RecoveryState &recoveryState() const { return m_stateData; }

    int progressPercent() const;
    int filesFound() const { return m_stateData.filesFound; }

public slots:
    void startRecovery(const RecoveryState &state);
    void pauseRecovery();
    void resumeRecovery();
    void cancelRecovery();

signals:
    void stateChanged(RecoveryEngine::State newState);
    void progressUpdated(int percent, qint64 offset, qint64 total);
    void chunkStarted(int chunkNumber, qint64 offset);
    void chunkFinished(int chunkNumber, int filesInChunk);
    void fileRecovered(const QString &filePath, const QString &type);
    void logMessage(LogEntry::Level level, const QString &message);
    void finished(bool success);

private slots:
    void processNextChunk();
    void onDdFinished(int exitCode, QProcess::ExitStatus status);
    void onForemostFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);

private:
    void setState(State s);
    void runDd();
    void runForemost();
    void moveFilesToOutput();
    void saveCurrentState();
    void cleanupCurrentChunk();
    QString chunkFileName(int chunk) const;
    QString foremostOutputDir(int chunk) const;
    QString fileTypeFromExt(const QString &ext) const;
    QString buildForemostTypes() const;

    State m_state = Idle;
    RecoveryState m_stateData;
    LogModel *m_log;
    QProcess *m_currentProcess = nullptr;
    QElapsedTimer m_elapsed;
    int m_chunkFilesCount = 0;
    bool m_hasSavedState = false;
};

#endif
