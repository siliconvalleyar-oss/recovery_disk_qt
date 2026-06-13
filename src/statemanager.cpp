#include "statemanager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>

StateManager::StateManager(QObject *parent) : QObject(parent) {}

QString StateManager::configDir() {
    QString dir = QDir::homePath() + "/.config/recovery_qt";
    QDir().mkpath(dir);
    return dir;
}

QString StateManager::stateFilePath() const {
    return configDir() + "/state.json";
}

void StateManager::saveState(const RecoveryState &state) {
    QJsonObject json = toJson(state);
    QFile file(stateFilePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(json).toJson());
        file.close();
        emit stateSaved();
    }
}

RecoveryState StateManager::loadState() {
    QFile file(stateFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return RecoveryState();
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (doc.isNull() || !doc.isObject())
        return RecoveryState();
    RecoveryState state = fromJson(doc.object());
    emit stateLoaded(state);
    return state;
}

bool StateManager::hasSavedState() {
    return QFile::exists(stateFilePath());
}

void StateManager::clearState() {
    QFile::remove(stateFilePath());
}

RecoveryState StateManager::fromJson(const QJsonObject &json) const {
    RecoveryState s;
    s.version = json["version"].toInt(1);
    s.partition = json["partition"].toString("/dev/sdd3");
    s.totalSize = json["total_size"].toVariant().toLongLong();
    s.chunkSizeMb = json["chunk_size_mb"].toInt(500);
    s.tool = json["tool"].toString("foremost");
    s.outputDir = json["output_dir"].toString();
    s.tmpDir = json["tmp_dir"].toString("/tmp/recovery_qt");
    s.currentChunk = json["current_chunk"].toInt(0);
    s.offsetBytes = json["offset_bytes"].toVariant().toLongLong();
    s.status = json["status"].toString("idle");
    s.filesFound = json["files_found"].toInt(0);
    s.filesByType = json["files_by_type"].toObject();
    s.elapsedMs = json["elapsed_ms"].toVariant().toLongLong();
    s.startedAt = QDateTime::fromString(json["started_at"].toString(), Qt::ISODate);
    s.pausedAt = QDateTime::fromString(json["paused_at"].toString(), Qt::ISODate);

    s.excludeWindows = json["exclude_windows"].toBool(true);
    s.deepScan = json["deep_scan"].toBool(false);
    s.organizeByType = json["organize_by_type"].toBool(true);
    s.reportOnly = json["report_only"].toBool(false);
    s.useStrings = json["use_strings"].toBool(false);
    s.compressZip = json["compress_zip"].toBool(false);
    s.diskModel = json["disk_model"].toString();
    s.smartHealth = json["smart_health"].toString();
    s.logFilePath = json["log_file_path"].toString();
    s.reportFilePath = json["report_file_path"].toString();

    const QJsonArray arr = json["file_types"].toArray();
    for (const auto &v : arr)
        s.fileTypes.append(v.toString());

    return s;
}

QJsonObject StateManager::toJson(const RecoveryState &state) const {
    QJsonObject json;
    json["version"] = state.version;
    json["partition"] = state.partition;
    json["total_size"] = state.totalSize;
    json["chunk_size_mb"] = state.chunkSizeMb;
    json["tool"] = state.tool;
    json["output_dir"] = state.outputDir;
    json["tmp_dir"] = state.tmpDir;
    json["current_chunk"] = state.currentChunk;
    json["offset_bytes"] = state.offsetBytes;
    json["status"] = state.status;
    json["files_found"] = state.filesFound;
    json["files_by_type"] = state.filesByType;
    json["elapsed_ms"] = state.elapsedMs;
    json["started_at"] = state.startedAt.toString(Qt::ISODate);
    json["paused_at"] = state.pausedAt.toString(Qt::ISODate);

    json["exclude_windows"] = state.excludeWindows;
    json["deep_scan"] = state.deepScan;
    json["organize_by_type"] = state.organizeByType;
    json["report_only"] = state.reportOnly;
    json["use_strings"] = state.useStrings;
    json["compress_zip"] = state.compressZip;
    json["disk_model"] = state.diskModel;
    json["smart_health"] = state.smartHealth;
    json["log_file_path"] = state.logFilePath;
    json["report_file_path"] = state.reportFilePath;

    QJsonArray arr;
    for (const auto &ft : state.fileTypes)
        arr.append(ft);
    json["file_types"] = arr;

    return json;
}
