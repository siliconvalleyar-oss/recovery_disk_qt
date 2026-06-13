#include "recoveryengine.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>
#include <QProcess>
#include <QStorageInfo>
#include <QRegExp>
#include <cstdlib>

RecoveryEngine::RecoveryEngine(LogModel *logModel, QObject *parent)
    : QObject(parent), m_log(logModel)
{
}

RecoveryEngine::~RecoveryEngine() {
    if (m_logFile.isOpen())
        m_logFile.close();
    if (m_currentProcess) {
        m_currentProcess->kill();
        m_currentProcess->waitForFinished(3000);
    }
}

int RecoveryEngine::progressPercent() const {
    if (m_stateData.totalSize <= 0) return 0;
    return static_cast<int>((m_stateData.offsetBytes * 100) / m_stateData.totalSize);
}

QString RecoveryEngine::checkSmartHealth(const QString &partition) {
    QProcess proc;
    proc.start("smartctl", QStringList() << "-H" << partition);
    proc.waitForFinished(5000);
    QString output = proc.readAllStandardOutput();
    for (const auto &line : output.split('\n')) {
        if (line.contains("SMART overall-health") || line.contains("SMART Health Status")) {
            auto parts = line.split(": ");
            if (parts.size() >= 2)
                return parts.last().trimmed();
        }
    }
    if (!proc.readAllStandardError().isEmpty())
        return "smartctl no disponible";
    return "No disponible";
}

qint64 RecoveryEngine::checkFreeSpace(const QString &path) {
    QStorageInfo storage(path);
    if (storage.isValid())
        return storage.bytesAvailable();
    return 0;
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

QString RecoveryEngine::photorecOutputDir() const {
    return m_stateData.outputDir + "/photorec";
}

QString RecoveryEngine::stringsOutputFile() const {
    return m_stateData.outputDir + "/strings_encontradas.txt";
}

QString RecoveryEngine::fileTypeFromExt(const QString &ext) const {
    QString e = ext.toLower();
    if (m_stateData.wordExts.contains(e)) return "Word";
    if (m_stateData.excelExts.contains(e)) return "Excel";
    if (m_stateData.pptExts.contains(e)) return "PowerPoint";
    if (m_stateData.pdfExts.contains(e)) return "PDF";
    if (m_stateData.rtfExts.contains(e)) return "RTF";
    if (m_stateData.textExts.contains(e)) return "Texto";
    return "Otros";
}

QString RecoveryEngine::logTimestamp() const {
    return QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
}

void RecoveryEngine::logToFile(const QString &message) {
    if (m_logFile.isOpen()) {
        QTextStream out(&m_logFile);
        out << logTimestamp() << " - " << message << "\n";
        out.flush();
    }
}

void RecoveryEngine::startRecovery(const RecoveryState &state) {
    m_stateData = state;
    m_stateData.status = "running";
    m_stateData.startedAt = QDateTime::currentDateTime();
    m_stateData.elapsedMs = 0;
    m_allRecoveredFiles.clear();

    // Setup log file
    QString logDir = m_stateData.outputDir + "/logs";
    QDir().mkpath(logDir);
    m_stateData.logFilePath = logDir + "/recovery_" +
        QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".log";
    m_logFile.setFileName(m_stateData.logFilePath);
    if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&m_logFile);
        out << "============================================\n";
        out << "  RECUPERACION DE DOCUMENTOS - LOG\n";
        out << "  Particion: " << m_stateData.partition << "\n";
        out << "  Inicio:    " << logTimestamp() << "\n";
        out << "============================================\n\n";
        out.flush();
    }

    QDir().mkpath(m_stateData.tmpDir);
    QDir().mkpath(m_stateData.outputDir);

    m_elapsed.start();
    setState(Running);
    saveCurrentState();

    QString msg = QString("Iniciando recuperacion de %1 (%2 GB)")
        .arg(m_stateData.partition)
        .arg(m_stateData.totalSize / 1e9, 0, 'f', 1);
    emit logMessage(LogEntry::Info, msg);
    logToFile(msg);

    if (m_stateData.currentChunk > 0) {
        QString resumeMsg = QString("Reanudando desde chunk %1 (offset %2 MB)")
            .arg(m_stateData.currentChunk)
            .arg(m_stateData.offsetBytes / 1e6, 0, 'f', 0);
        emit logMessage(LogEntry::Info, resumeMsg);
        logToFile(resumeMsg);
    }

    // Check free space
    qint64 freeBytes = checkFreeSpace(m_stateData.outputDir);
    if (freeBytes > 0 && freeBytes < m_stateData.totalSize) {
        QString warnMsg = QString("Espacio libre: %1 GB - Puede ser insuficiente para %2 GB")
            .arg(freeBytes / 1e9, 0, 'f', 1)
            .arg(m_stateData.totalSize / 1e9, 0, 'f', 1);
        emit logMessage(LogEntry::Warn, warnMsg);
        logToFile(warnMsg);
    }

    // If report only, do quick scan
    if (m_stateData.reportOnly) {
        m_currentPhase = PhaseForemost;
        runForemost();
        return;
    }

    processNextChunk();
}

void RecoveryEngine::pauseRecovery() {
    if (m_state != Running) return;
    setState(Pausing);
    emit logMessage(LogEntry::Warn, "Pausando recuperacion...");
    logToFile("Pausando recuperacion...");

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
    logToFile("Recuperacion pausada.");
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

    // Reopen log file
    if (!m_stateData.logFilePath.isEmpty()) {
        m_logFile.setFileName(m_stateData.logFilePath);
        if (m_stateData.logFilePath.contains('/')) {
            QDir().mkpath(QFileInfo(m_stateData.logFilePath).absolutePath());
        }
        if (m_logFile.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&m_logFile);
            out << "\n--- REANUDACION ---\n";
        }
    }

    saveCurrentState();

    emit logMessage(LogEntry::Info,
        QString("Reanudando recuperacion - chunk %1, %2 archivos encontrados hasta ahora")
            .arg(m_stateData.currentChunk)
            .arg(m_stateData.filesFound));
    logToFile(QString("Reanudando - chunk %1").arg(m_stateData.currentChunk));

    processNextChunk();
}

void RecoveryEngine::cancelRecovery() {
    if (m_currentProcess && m_currentProcess->state() != QProcess::NotRunning) {
        m_currentProcess->kill();
        m_currentProcess->waitForFinished(3000);
    }
    cleanupCurrentChunk();
    StateManager().clearState();
    m_stateData.status = "cancelled";
    if (m_logFile.isOpen()) {
        logToFile("Recuperacion cancelada por el usuario.");
        m_logFile.close();
    }
    setState(Idle);
    emit logMessage(LogEntry::Warn, "Recuperacion cancelada por el usuario.");
    emit finished(false);
}

void RecoveryEngine::processNextChunk() {
    if (m_state == Pausing || m_state == Paused) return;

    qint64 chunkBytes = static_cast<qint64>(m_stateData.chunkSizeMb) * 1024 * 1024;

    if (m_stateData.offsetBytes >= m_stateData.totalSize) {
        // All chunks done - now run post-processing tools
        m_currentPhase = PhaseForemost;
        runNextTool();
        return;
    }

    qint64 remaining = m_stateData.totalSize - m_stateData.offsetBytes;
    qint64 thisChunk = qMin(chunkBytes, remaining);

    emit chunkStarted(m_stateData.currentChunk, m_stateData.offsetBytes);
    QString msg = QString("Chunk %1: offset %2 MB, tamano %3 MB")
        .arg(m_stateData.currentChunk)
        .arg(m_stateData.offsetBytes / 1e6, 0, 'f', 0)
        .arg(thisChunk / 1e6, 0, 'f', 0);
    emit logMessage(LogEntry::Info, msg);
    logToFile(msg);

    m_currentPhase = PhaseDd;
    runDd();
}

void RecoveryEngine::runNextTool() {
    if (m_state == Pausing || m_state == Paused) return;

    switch (m_currentPhase) {
    case PhaseDd:
        m_currentPhase = PhaseForemost;
        runForemost();
        break;
    case PhaseForemost:
        if (m_stateData.deepScan) {
            m_currentPhase = PhasePhotorec;
            runPhotorec();
        } else if (m_stateData.useStrings) {
            m_currentPhase = PhaseStrings;
            runStrings();
        } else {
            m_currentPhase = PhaseReport;
            generateReport();
        }
        break;
    case PhasePhotorec:
        if (m_stateData.useStrings) {
            m_currentPhase = PhaseStrings;
            runStrings();
        } else {
            m_currentPhase = PhaseReport;
            generateReport();
        }
        break;
    case PhaseStrings:
        if (m_stateData.compressZip) {
            m_currentPhase = PhaseZip;
            runZipCompression();
        } else {
            m_currentPhase = PhaseReport;
            generateReport();
        }
        break;
    case PhaseZip:
        m_currentPhase = PhaseReport;
        generateReport();
        break;
    case PhaseReport:
        m_stateData.status = "completed";
        m_stateData.elapsedMs += m_elapsed.elapsed();
        saveCurrentState();
        StateManager().clearState();
        if (m_logFile.isOpen()) {
            logToFile("Recuperacion completada exitosamente.");
            m_logFile.close();
        }
        setState(Completed);
        emit logMessage(LogEntry::Ok, "Recuperacion completada exitosamente.");
        emit finished(true);
        break;
    default:
        break;
    }
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

    QString ddMsg = QString("dd: extrayendo chunk (skip=%1M, count=%2M)...").arg(skipMb).arg(countMb);
    emit logMessage(LogEntry::Info, ddMsg);
    m_currentProcess->start();
}

void RecoveryEngine::onDdFinished(int exitCode, QProcess::ExitStatus status) {
    if (m_state == Pausing || m_state == Paused) return;

    if (status != QProcess::NormalExit || exitCode != 0) {
        emit logMessage(LogEntry::Warn,
            QString("dd termino con codigo %1 (puede ser normal al final del disco)").arg(exitCode));
    }

    m_currentPhase = PhaseForemost;
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

    QString types = buildForemostTypes();

    if (m_stateData.reportOnly) {
        // Quick audit mode - scan full disk without chunking
        QString auditFile = m_stateData.outputDir + "/foremost_audit.txt";

        QStringList args;
        args << "-t" << types
             << "-i" << m_stateData.partition
             << "-o" << m_stateData.tmpDir + "/report_audit"
             << "-q" << "-v";

        m_currentProcess->setProgram("foremost");
        m_currentProcess->setArguments(args);

        emit logMessage(LogEntry::Info, "foremost: escaneo rapido de cabeceras (modo reporte)...");
        m_currentProcess->start();
        return;
    }

    QString chunkFile = chunkFileName(m_stateData.currentChunk);
    QString outDir = foremostOutputDir(m_stateData.currentChunk);
    QDir().mkpath(outDir);

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
        if (!m_stateData.reportOnly) {
            cleanupCurrentChunk();
            m_stateData.offsetBytes += static_cast<qint64>(m_stateData.chunkSizeMb) * 1024 * 1024;
            m_stateData.currentChunk++;
            saveCurrentState();
            processNextChunk();
        } else {
            // In report mode, move to next phase
            runNextTool();
        }
        return;
    }

    if (exitCode != 0) {
        emit logMessage(LogEntry::Warn,
            QString("foremost termino con codigo %1").arg(exitCode));
    }

    if (m_stateData.reportOnly) {
        // Copy audit file
        QString auditSrc = m_stateData.tmpDir + "/report_audit/audit.txt";
        if (QFile::exists(auditSrc)) {
            QFile::copy(auditSrc, m_stateData.outputDir + "/foremost_audit.txt");
            emit logMessage(LogEntry::Ok, "Reporte de foremost generado.");
        }
        runNextTool(); // goes to strings or report
        return;
    }

    // Normal chunk processing
    if (m_stateData.deepScan) {
        // In deep scan mode, move files but also run photorec on the chunk
        moveFilesToOutput();
        m_currentPhase = PhasePhotorec;
        QString chunkFile = chunkFileName(m_stateData.currentChunk);
        // photorec works on the chunk too
        runPhotorec();
    } else {
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
}

void RecoveryEngine::runPhotorec() {
    if (m_currentProcess) {
        m_currentProcess->deleteLater();
        m_currentProcess = nullptr;
    }

    m_currentProcess = new QProcess(this);
    connect(m_currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RecoveryEngine::onPhotorecFinished);
    connect(m_currentProcess, &QProcess::errorOccurred, this, &RecoveryEngine::onProcessError);

    QString prDir;
    QString inputSource = m_stateData.partition;

    if (!m_stateData.reportOnly) {
        prDir = m_stateData.outputDir + "/photorec_chunk_" +
                QString::number(m_stateData.currentChunk).rightJustified(4, '0');
        // Use chunk file for faster operation
        QString chunkFile = chunkFileName(m_stateData.currentChunk);
        if (QFile::exists(chunkFile))
            inputSource = chunkFile;
    } else {
        prDir = m_stateData.tmpDir + "/photorec_report";
    }

    QDir().mkpath(prDir);

    // Photorec command with document filter
    QStringList args;
    args << "/log" << "/d" << prDir
         << "/cmd" << inputSource
         << "fileopt,everything,disable,doc,enable,zip,enable,txt,enable,search";

    m_currentProcess->setProgram("photorec");
    m_currentProcess->setArguments(args);

    emit logMessage(LogEntry::Info,
        QString("photorec: recuperando documentos de %1 ...").arg(inputSource));
    logToFile("Iniciando photorec...");
    m_currentProcess->start();
}

void RecoveryEngine::onPhotorecFinished(int exitCode, QProcess::ExitStatus status) {
    if (m_state == Pausing || m_state == Paused) return;

    if (exitCode != 0 || status != QProcess::NormalExit) {
        emit logMessage(LogEntry::Warn,
            QString("photorec termino con codigo %1").arg(exitCode));
    }

    // Move photorec results
    QString prDir;
    if (!m_stateData.reportOnly) {
        prDir = m_stateData.outputDir + "/photorec_chunk_" +
                QString::number(m_stateData.currentChunk).rightJustified(4, '0');
    } else {
        prDir = m_stateData.tmpDir + "/photorec_report";
    }

    if (QDir(prDir).exists()) {
        QStringList docExts = {"doc","docx","xls","xlsx","ppt","pptx","pdf","rtf","txt","csv"};
        QStringList winExts = {"dll","exe","sys","drv","cpl","ocx","com","scr","msi","msp"};

        QDirIterator it(prDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        int photorecCount = 0;
        while (it.hasNext()) {
            QString filePath = it.next();
            QFileInfo fi(filePath);
            QString ext = fi.suffix().toLower();

            if (!docExts.contains(ext)) continue;
            if (m_stateData.excludeWindows && winExts.contains(ext)) continue;
            if (fi.size() < 100) continue;

            QString category = fileTypeFromExt(ext);
            QString destBase = m_stateData.outputDir;
            if (m_stateData.organizeByType) {
                destBase += "/" + category;
            }
            QDir().mkpath(destBase);

            QString destPath = destBase + "/" + fi.fileName();
            if (QFile::exists(destPath)) {
                destPath = destBase + "/" + fi.completeBaseName() + "_pr_"
                           + QString::number(m_stateData.currentChunk) + "." + ext;
            }

            if (QFile::copy(filePath, destPath)) {
                m_stateData.filesFound++;
                photorecCount++;
                m_allRecoveredFiles << destPath;

                QJsonObject byType = m_stateData.filesByType;
                byType[category] = byType[category].toInt(0) + 1;
                m_stateData.filesByType = byType;

                emit fileRecovered(destPath, category);
            }
        }

        // Remove photorec temp dir
        QDir(prDir).removeRecursively();

        if (photorecCount > 0) {
            emit logMessage(LogEntry::Ok,
                QString("photorec: %1 archivos recuperados en chunk %2")
                    .arg(photorecCount).arg(m_stateData.currentChunk));
        }
    }

    if (m_stateData.reportOnly) {
        runNextTool();
        return;
    }

    // Continue chunk processing
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

void RecoveryEngine::runStrings() {
    if (m_currentProcess) {
        m_currentProcess->deleteLater();
        m_currentProcess = nullptr;
    }

    m_currentProcess = new QProcess(this);
    connect(m_currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RecoveryEngine::onStringsFinished);
    connect(m_currentProcess, &QProcess::errorOccurred, this, &RecoveryEngine::onProcessError);

    if (m_stateData.reportOnly) {
        // Quick strings on partition
        QStringList args;
        args << "if=" + m_stateData.partition << "bs=1M";

        QProcess *ddProc = new QProcess(this);
        ddProc->setProgram("dd");
        ddProc->setArguments(args);
        ddProc->setStandardOutputProcess(m_currentProcess);

        QString strFile = stringsOutputFile();
        QDir().mkpath(QFileInfo(strFile).absolutePath());
        m_currentProcess->setStandardOutputFile(strFile);

        QStringList strArgs;
        strArgs << "-n" << "50";
        m_currentProcess->setProgram("strings");
        m_currentProcess->setArguments(strArgs);

        emit logMessage(LogEntry::Info, "dd+strings: buscando cadenas de texto en la particion...");
        logToFile("Iniciando extraccion de strings...");
        ddProc->start();
        m_currentProcess->start();
    } else {
        emit logMessage(LogEntry::Info, "strings: escaneo de cadenas completado (post-chunk).");
        runNextTool();
    }
}

void RecoveryEngine::onStringsFinished(int exitCode, QProcess::ExitStatus status) {
    if (m_state == Pausing || m_state == Paused) return;

    QString strFile = stringsOutputFile();
    if (QFile::exists(strFile)) {
        QFile f(strFile);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            QString content = in.readAll();
            m_stringsFoundCount = content.count('\n');

            // Filter relevant strings (document-related content)
            QString filteredFile = m_stateData.outputDir + "/strings_documentos_referencia.txt";
            QFile fout(filteredFile);
            if (fout.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&fout);
                QStringList lines = content.split('\n');
                int count = 0;
                for (const auto &line : lines) {
                    if (line.contains(QRegExp("\\.doc|\\.docx|\\.xls|\\.xlsx|\\.pdf|documento|informe|report|tesis|carta|contrato|factura|presupuesto", Qt::CaseInsensitive))) {
                        out << line << "\n";
                        count++;
                        if (count >= 200) break;
                    }
                }
                fout.close();
            }
            f.close();
        }

        emit logMessage(LogEntry::Ok,
            QString("strings: %1 lineas extraidas. Referencias filtradas en: %2")
                .arg(m_stringsFoundCount)
                .arg(m_stateData.outputDir + "/strings_documentos_referencia.txt"));
        logToFile(QString("Strings extraidas: %1 lineas").arg(m_stringsFoundCount));
    }

    if (m_stateData.reportOnly) {
        runNextTool();
        return;
    }
    runNextTool();
}

void RecoveryEngine::runZipCompression() {
    if (m_currentProcess) {
        m_currentProcess->deleteLater();
        m_currentProcess = nullptr;
    }

    m_currentProcess = new QProcess(this);
    connect(m_currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        emit logMessage(LogEntry::Ok,
            QString("ZIP creado: %1/recuperados.zip").arg(m_stateData.outputDir));
        logToFile("Compresion ZIP completada.");
        runNextTool();
    });
    connect(m_currentProcess, &QProcess::errorOccurred, this, &RecoveryEngine::onProcessError);

    QString zipFile = m_stateData.outputDir + "/recuperados.zip";

    QStringList args;
    args << "-r" << zipFile << ".";

    emit logMessage(LogEntry::Info, "Comprimiendo resultados en ZIP...");
    logToFile("Comprimiendo archivos recuperados...");

    m_currentProcess->setWorkingDirectory(m_stateData.outputDir);
    m_currentProcess->setProgram("zip");
    m_currentProcess->setArguments(args);
    m_currentProcess->start();
}

void RecoveryEngine::generateReport() {
    QString reportFile = m_stateData.outputDir + "/reporte_final.txt";
    m_stateData.reportFilePath = reportFile;

    QFile file(reportFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);

        out << "============================================\n";
        out << "  REPORTE FINAL DE RECUPERACION\n";
        out << "  Particion: " << m_stateData.partition << "\n";
        out << "  Fecha:     " << logTimestamp() << "\n";
        out << "============================================\n\n";

        out << "  Directorio de salida: " << m_stateData.outputDir << "\n";
        out << "  Log:                  " << m_stateData.logFilePath << "\n";
        out << "  Reporte:              " << reportFile << "\n";

        if (!m_stateData.diskModel.isEmpty())
            out << "  Disco:                " << m_stateData.diskModel << "\n";
        if (!m_stateData.smartHealth.isEmpty())
            out << "  Salud SMART:          " << m_stateData.smartHealth << "\n";

        qint64 elapsed = m_stateData.elapsedMs / 1000;
        out << "  Tiempo total:         " << elapsed << "s\n";
        out << "\n";

        out << "--- ARCHIVOS POR TIPO ---\n\n";
        int total = 0;
        QJsonObject byType = m_stateData.filesByType;
        QStringList categories = {"Word", "Excel", "PowerPoint", "PDF", "RTF", "Texto", "Otros"};
        for (const auto &cat : categories) {
            int count = byType[cat].toInt(0);
            if (count > 0) {
                out << "  " << cat << ": " << count << " archivos\n";
                total += count;
            }
        }

        out << "\n  TOTAL: " << total << " documentos recuperados\n\n";

        if (m_stateData.deepScan)
            out << "  Modo: busqueda profunda (foremost + photorec)\n";
        else
            out << "  Modo: busqueda estandar (foremost)\n";

        if (m_stateData.useStrings)
            out << "  Strings extraidas: " << m_stringsFoundCount << " lineas\n";
        if (m_stateData.compressZip)
            out << "  Resultados comprimidos en: recuperados.zip\n";

        out << "\n============================================\n";
        out << "  RECUPERACION COMPLETADA\n";
        out << "============================================\n";

        file.close();
    }

    emit logMessage(LogEntry::Ok, QString("Reporte generado: %1").arg(reportFile));
    logToFile(QString("Reporte generado: %1").arg(reportFile));

    runNextTool();
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

        // Filter small files (< 1KB false positives)
        if (fi.size() < 1024) continue;

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
            m_allRecoveredFiles << destPath;

            QJsonObject byType = m_stateData.filesByType;
            byType[category] = byType[category].toInt(0) + 1;
            m_stateData.filesByType = byType;

            emit fileRecovered(destPath, category);
        }
    }

    // Also run the Windows filter on the output directory periodically
    if (m_stateData.excludeWindows) {
        QStringList winPatterns = {"*.dll","*.exe","*.sys","*.drv","*.cpl","*.ocx","*.com","*.scr","*.msi","*.msp"};
        for (const auto &pattern : winPatterns) {
            QDirIterator delIt(m_stateData.outputDir, {pattern}, QDir::Files, QDirIterator::Subdirectories);
            while (delIt.hasNext()) {
                QFile::remove(delIt.next());
            }
        }
        // Remove files < 1KB
        QDirIterator delIt(m_stateData.outputDir, QDir::Files, QDirIterator::Subdirectories);
        while (delIt.hasNext()) {
            QString f = delIt.next();
            if (QFileInfo(f).size() < 1024)
                QFile::remove(f);
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
    case QProcess::FailedToStart: msg = "No se pudo iniciar el proceso. Verifique que la herramienta esta instalada."; break;
    case QProcess::Crashed:       msg = "El proceso se detuvo inesperadamente."; break;
    case QProcess::Timedout:      msg = "Tiempo de espera agotado."; break;
    case QProcess::WriteError:    msg = "Error de escritura en el proceso."; break;
    case QProcess::ReadError:     msg = "Error de lectura del proceso."; break;
    default:                      msg = "Error desconocido en el proceso."; break;
    }
    emit logMessage(LogEntry::Error, msg);
    logToFile("ERROR: " + msg);
}
