#include "recoveryengine.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>
#include <cstdlib>

RecoveryEngine::RecoveryEngine(LogModel *logModel, QObject *parent)
    : QObject(parent), m_log(logModel)
{
}

RecoveryEngine::~RecoveryEngine() {
    if (m_currentProcess) {
        m_currentProcess->kill();
        m_currentProcess->waitForFinished(3000);
    }
}

int RecoveryEngine::progressPercent() const {
    if (m_stateData.totalSize <= 0) return 0;
    return static_cast<int>((m_stateData.offsetBytes * 100) / m_stateData.totalSize);
}

void RecoveryEngine::setState(State s) {
    if (m_state != s) {
        m_state = s;
        emit stateChanged(s);
    }
}

QString RecoveryEngine::buildForemostTypes() const {
    return m_stateData.fileTypes.join(",");
}

QString RecoveryEngine::chunkFileName(int chunk) const {
    return QString("%1/chunk_%2.dat").arg(m_stateData.tmpDir).arg(chunk, 4, 10, QChar('0'));
}

QString RecoveryEngine::foremostOutputDir(int chunk) const {
    return QString("%1/foremost_%2").arg(m_stateData.tmpDir).arg(chunk, 4, 10, QChar('0'));
}

QString RecoveryEngine::fileTypeFromExt(const QString &ext) const {
    QString e = ext.toLower();
    if (e == "doc" || e == "docx" || e == "dot" || e == "dotx" || e == "docm") return "Word";
    if (e == "xls" || e == "xlsx" || e == "xlsm" || e == "csv") return "Excel";
    if (e == "ppt" || e == "pptx" || e == "pps" || e == "ppsx") return "PowerPoint";
    if (e == "pdf") return "PDF";
    if (e == "rtf") return "RTF";
    if (e == "txt" || e == "text" || e == "log" || e == "md") return "Texto";
    return "Otros";
}

void RecoveryEngine::startRecovery(const RecoveryState &state) {
    m_stateData = state;
    m_stateData.status = "running";
    m_stateData.startedAt = QDateTime::currentDateTime();
    m_stateData.elapsedMs = 0;

    QDir().mkpath(m_stateData.tmpDir);
    QDir().mkpath(m_stateData.outputDir);

    m_elapsed.start();
    setState(Running);
    saveCurrentState();

    emit logMessage(LogEntry::Info,
        QString("Iniciando recuperacion de %1 (%2 GB)")
            .arg(m_stateData.partition)
            .arg(m_stateData.totalSize / 1e9, 0, 'f', 1));

    if (m_stateData.currentChunk > 0) {
        emit logMessage(LogEntry::Info,
            QString("Reanudando desde chunk %1 (offset %2 MB)")
                .arg(m_stateData.currentChunk)
                .arg(m_stateData.offsetBytes / 1e6, 0, 'f', 0));
    }

    processNextChunk();
}

void RecoveryEngine::pauseRecovery() {
    if (m_state != Running) return;
    setState(Pausing);
    emit logMessage(LogEntry::Warn, "Pausando recuperacion...");

    if (m_currentProcess && m_currentProcess->state() != QProcess::NotRunning) {
        m_currentProcess->kill();
        m_currentProcess->waitForFinished(3000);
    }

    m_stateData.status = "paused";
    m_stateData.elapsedMs += m_elapsed.elapsed();
    m_stateData.pausedAt = QDateTime::currentDateTime();
    saveCurrentState();

    cleanupCurrentChunk();
    setState(Paused);
    emit logMessage(LogEntry::Ok, "Recuperacion pausada. Puede reanudar despues de reiniciar.");
}

void RecoveryEngine::resumeRecovery() {
    RecoveryState saved = StateManager().loadState();
    if (!saved.isValid() || saved.status != "paused") {
        emit logMessage(LogEntry::Error, "No hay estado de pausa para reanudar.");
        return;
    }

    m_stateData = saved;
    m_stateData.status = "running";
    m_elapsed.start();
    setState(Running);
    saveCurrentState();

    emit logMessage(LogEntry::Info,
        QString("Reanudando recuperacion - chunk %1, %2 archivos encontrados hasta ahora")
            .arg(m_stateData.currentChunk)
            .arg(m_stateData.filesFound));

    processNextChunk();
}

void RecoveryEngine::cancelRecovery() {
    if (m_currentProcess && m_currentProcess->state() != QProcess::NotRunning) {
        m_currentProcess->kill();
        m_currentProcess->waitForFinished(3000);
    }
    cleanupCurrentChunk();
    StateManager().clearState();
    setState(Idle);
    emit logMessage(LogEntry::Warn, "Recuperacion cancelada por el usuario.");
    emit finished(false);
}

void RecoveryEngine::processNextChunk() {
    if (m_state == Pausing || m_state == Paused) return;

    qint64 chunkBytes = static_cast<qint64>(m_stateData.chunkSizeMb) * 1024 * 1024;

    if (m_stateData.offsetBytes >= m_stateData.totalSize) {
        m_stateData.status = "completed";
        m_stateData.elapsedMs += m_elapsed.elapsed();
        saveCurrentState();
        StateManager().clearState();
        setState(Completed);
        emit logMessage(LogEntry::Ok, "Recuperacion completada exitosamente.");
        emit finished(true);
        return;
    }

    qint64 remaining = m_stateData.totalSize - m_stateData.offsetBytes;
    qint64 thisChunk = qMin(chunkBytes, remaining);

    emit chunkStarted(m_stateData.currentChunk, m_stateData.offsetBytes);
    emit logMessage(LogEntry::Info,
        QString("Chunk %1: offset %2 MB, tamano %3 MB")
            .arg(m_stateData.currentChunk)
            .arg(m_stateData.offsetBytes / 1e6, 0, 'f', 0)
            .arg(thisChunk / 1e6, 0, 'f', 0));

    // If reportOnly, skip actual extraction
    if (m_stateData.reportOnly) {
        emit logMessage(LogEntry::Info, "Modo reporte: escaneando cabeceras de archivos...");
        QString types = buildForemostTypes();
        QString auditFile = m_stateData.outputDir + "/foremost_audit.txt";

        QProcess foremost;
        QStringList args;
        args << "-t" << types << "-i" << m_stateData.partition
             << "-o" << m_stateData.tmpDir + "/report_audit" << "-q" << "-v";
        foremost.start("foremost", args);
        foremost.waitForFinished(30000);

        m_stateData.status = "completed";
        saveCurrentState();
        StateManager().clearState();
        setState(Completed);
        emit progressUpdated(100, m_stateData.totalSize, m_stateData.totalSize);
        emit logMessage(LogEntry::Ok, "Reporte generado. Ejecute 'completa' para extraer archivos.");
        emit finished(true);
        return;
    }

    runDd();
}

void RecoveryEngine::runDd() {
    if (m_currentProcess) {
        m_currentProcess->deleteLater();
        m_currentProcess = nullptr;
    }

    m_currentProcess = new QProcess(this);
    connect(m_currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RecoveryEngine::onDdFinished);
    connect(m_currentProcess, &QProcess::errorOccurred, this, &RecoveryEngine::onProcessError);

    QString chunkFile = chunkFileName(m_stateData.currentChunk);
    QDir().mkpath(QFileInfo(chunkFile).absolutePath());

    qint64 chunkBytes = static_cast<qint64>(m_stateData.chunkSizeMb) * 1024 * 1024;
    qint64 remaining = m_stateData.totalSize - m_stateData.offsetBytes;
    qint64 countBytes = qMin(chunkBytes, remaining);
    int countMb = static_cast<int>(countBytes / (1024 * 1024));
    int skipMb = static_cast<int>(m_stateData.offsetBytes / (1024 * 1024));

    QStringList args;
    args << "if=" + m_stateData.partition
         << "bs=1M"
         << "skip=" + QString::number(skipMb)
         << "count=" + QString::number(qMax(countMb, 1))
         << "of=" + chunkFile;

    m_currentProcess->setProgram("dd");
    m_currentProcess->setArguments(args);

    emit logMessage(LogEntry::Info,
        QString("dd: extrayendo chunk (skip=%1M, count=%2M)...").arg(skipMb).arg(countMb));
    m_currentProcess->start();
}

void RecoveryEngine::onDdFinished(int exitCode, QProcess::ExitStatus status) {
    if (m_state == Pausing || m_state == Paused) return;

    if (status != QProcess::NormalExit || exitCode != 0) {
        emit logMessage(LogEntry::Warn,
            QString("dd termino con codigo %1 (puede ser normal al final del disco)").arg(exitCode));
    }

    runForemost();
}

void RecoveryEngine::runForemost() {
    if (m_currentProcess) {
        m_currentProcess->deleteLater();
        m_currentProcess = nullptr;
    }

    m_currentProcess = new QProcess(this);
    connect(m_currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RecoveryEngine::onForemostFinished);
    connect(m_currentProcess, &QProcess::errorOccurred, this, &RecoveryEngine::onProcessError);

    QString chunkFile = chunkFileName(m_stateData.currentChunk);
    QString outDir = foremostOutputDir(m_stateData.currentChunk);
    QDir().mkpath(outDir);

    QString types = buildForemostTypes();
    int chunkSizeMb = m_stateData.chunkSizeMb;

    m_chunkFilesCount = 0;

    QStringList args;
    args << "-t" << types
         << "-i" << chunkFile
         << "-o" << outDir
         << "-v" << "-q"
         << "-b" << "4096"
         << "-c" << "all";

    m_currentProcess->setProgram("foremost");
    m_currentProcess->setArguments(args);

    emit logMessage(LogEntry::Info, "foremost: escaneando chunk en busca de documentos...");
    m_currentProcess->start();
}

void RecoveryEngine::onForemostFinished(int exitCode, QProcess::ExitStatus status) {
    if (m_state == Pausing || m_state == Paused) return;

    if (status != QProcess::NormalExit) {
        emit logMessage(LogEntry::Error, "foremost: proceso termino anormalmente.");
        cleanupCurrentChunk();
        m_stateData.offsetBytes += static_cast<qint64>(m_stateData.chunkSizeMb) * 1024 * 1024;
        m_stateData.currentChunk++;
        saveCurrentState();
        processNextChunk();
        return;
    }

    if (exitCode != 0) {
        emit logMessage(LogEntry::Warn,
            QString("foremost termino con codigo %1 (puede ser normal)").arg(exitCode));
    }

    moveFilesToOutput();
    m_stateData.offsetBytes += static_cast<qint64>(m_stateData.chunkSizeMb) * 1024 * 1024;
    m_stateData.currentChunk++;
    m_stateData.elapsedMs = m_elapsed.elapsed();
    m_hasSavedState = false;

    emit progressUpdated(progressPercent(), m_stateData.offsetBytes, m_stateData.totalSize);
    emit chunkFinished(m_stateData.currentChunk - 1, m_chunkFilesCount);

    saveCurrentState();
    cleanupCurrentChunk();
    processNextChunk();
}

void RecoveryEngine::moveFilesToOutput() {
    QString outDir = foremostOutputDir(m_stateData.currentChunk);
    QDir dir(outDir);
    if (!dir.exists()) return;

    const QStringList docExts = {"doc","docx","xls","xlsx","ppt","pptx","pdf","rtf","txt","csv"};
    const QStringList winExts = {"dll","exe","sys","drv","cpl","ocx","com","scr","msi","msp"};

    QDirIterator it(outDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fi(filePath);
        QString ext = fi.suffix().toLower();

        if (fi.fileName() == "audit.txt") continue;
        if (!docExts.contains(ext)) continue;
        if (fi.size() < 100) continue;

        // Filter Windows system files
        if (m_stateData.excludeWindows && winExts.contains(ext)) continue;

        QString category = fileTypeFromExt(ext);
        QString destBase = m_stateData.outputDir;

        if (m_stateData.organizeByType) {
            destBase += "/" + category;
        }
        QDir().mkpath(destBase);

        QString destPath = destBase + "/" + fi.fileName();
        if (QFile::exists(destPath)) {
            destPath = destBase + "/" + fi.completeBaseName() + "_"
                       + QString::number(m_stateData.currentChunk) + "." + ext;
        }

        if (QFile::copy(filePath, destPath)) {
            m_stateData.filesFound++;
            m_chunkFilesCount++;

            QJsonObject byType = m_stateData.filesByType;
            byType[category] = byType[category].toInt(0) + 1;
            m_stateData.filesByType = byType;

            emit fileRecovered(destPath, category);
        }
    }
}

void RecoveryEngine::saveCurrentState() {
    StateManager sm;
    sm.saveState(m_stateData);
    m_hasSavedState = true;
}

void RecoveryEngine::cleanupCurrentChunk() {
    QString chunkFile = chunkFileName(m_stateData.currentChunk);
    QFile::remove(chunkFile);

    QString outDir = foremostOutputDir(m_stateData.currentChunk);
    QDir dir(outDir);
    if (dir.exists()) {
        dir.removeRecursively();
    }
}

void RecoveryEngine::onProcessError(QProcess::ProcessError error) {
    QString msg;
    switch (error) {
    case QProcess::FailedToStart: msg = "No se pudo iniciar el proceso."; break;
    case QProcess::Crashed:       msg = "El proceso se detuvo inesperadamente."; break;
    case QProcess::Timedout:      msg = "Tiempo de espera agotado."; break;
    case QProcess::WriteError:    msg = "Error de escritura en el proceso."; break;
    case QProcess::ReadError:     msg = "Error de lectura del proceso."; break;
    default:                      msg = "Error desconocido en el proceso."; break;
    }
    emit logMessage(LogEntry::Error, msg);
}
