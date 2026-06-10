#ifndef STATEMANAGER_H
#define STATEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QDateTime>

struct RecoveryState {
    int version = 2;
    QString partition = "/dev/sdd3";
    qint64 totalSize = 0;
    int chunkSizeMb = 500;
    QString tool = "foremost";
    QStringList fileTypes = {"doc","docx","xls","xlsx","ppt","pptx","pdf","rtf","txt","csv"};
    QString outputDir;
    QString tmpDir = "/tmp/recovery_qt";
    int currentChunk = 0;
    qint64 offsetBytes = 0;
    QString status = "idle";
    int filesFound = 0;
    QJsonObject filesByType;
    qint64 elapsedMs = 0;
    QDateTime startedAt;
    QDateTime pausedAt;

    // Advanced options
    bool excludeWindows = true;
    bool deepScan = false;
    bool organizeByType = true;
    bool reportOnly = false;
    QString diskModel;

    bool isValid() const { return !partition.isEmpty() && totalSize > 0; }
};

class StateManager : public QObject {
    Q_OBJECT
public:
    explicit StateManager(QObject *parent = nullptr);

    QString stateFilePath() const;

    void saveState(const RecoveryState &state);
    RecoveryState loadState();
    bool hasSavedState();
    void clearState();

    static QString configDir();

signals:
    void stateLoaded(const RecoveryState &state);
    void stateSaved();

private:
    RecoveryState fromJson(const QJsonObject &json) const;
    QJsonObject toJson(const RecoveryState &state) const;
};

#endif
