#include "mainwindow.h"
#include <QToolBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QTabWidget>
#include <QHeaderView>
#include <QGroupBox>
#include <QDirIterator>
#include <QGridLayout>
#include <QScrollBar>
#include <QApplication>
#include <QTimer>
#include <QDir>
#include <QDateTime>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Recuperacion Qt - Documentos");
    setMinimumSize(1000, 700);
    resize(1200, 800);

    m_logModel = new LogModel(this);
    m_engine = new RecoveryEngine(m_logModel, this);

    connect(m_engine, &RecoveryEngine::stateChanged, this, &MainWindow::onEngineStateChanged);
    connect(m_engine, &RecoveryEngine::progressUpdated, this, &MainWindow::onProgressUpdated);
    connect(m_engine, &RecoveryEngine::fileRecovered, this, &MainWindow::onFileRecovered);
    connect(m_engine, &RecoveryEngine::chunkStarted, this, &MainWindow::onChunkStarted);
    connect(m_engine, &RecoveryEngine::chunkFinished, this, &MainWindow::onChunkFinished);
    connect(m_engine, &RecoveryEngine::finished, this, &MainWindow::onFinished);
    connect(m_engine, &RecoveryEngine::logMessage, this, &MainWindow::onLogEntry);
    connect(m_logModel, &LogModel::entryAdded, this, &MainWindow::scrollToLastLog);

    setupUi();
    updateButtonStates();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi() {
    setupToolbar();

    auto *central = new QWidget;
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(8, 4, 8, 4);

    // ========== DISK INFO BAR ==========
    auto *diskGroup = new QGroupBox("Disco seleccionado");
    auto *diskLayout = new QHBoxLayout(diskGroup);

    m_diskLabel = new QLabel("Particion: --  |  Capacidad: --");
    m_diskLabel->setStyleSheet("font-weight: bold; color: #4fc3f7;");
    diskLayout->addWidget(m_diskLabel, 1);

    m_diskBar = new QProgressBar;
    m_diskBar->setRange(0, 100);
    m_diskBar->setValue(0);
    m_diskBar->setTextVisible(true);
    m_diskBar->setFixedWidth(200);
    m_diskBar->setFixedHeight(20);
    diskLayout->addWidget(m_diskBar);

    mainLayout->addWidget(diskGroup);

    // ========== STATS BAR ==========
    auto *statsGroup = new QGroupBox("Estado");
    auto *statsGrid = new QGridLayout(statsGroup);

    m_partitionLabel = new QLabel("Particion: --");
    m_modeLabel = new QLabel("Modo: --");
    m_modeLabel->setStyleSheet("color: #ffa726;");
    m_filesFoundLabel = new QLabel("Archivos: 0");
    m_filesFoundLabel->setStyleSheet("font-weight: bold; color: #4caf50;");
    m_chunksLabel = new QLabel("Chunks: 0");
    m_elapsedLabel = new QLabel("Tiempo: 0s");

    statsGrid->addWidget(m_partitionLabel, 0, 0);
    statsGrid->addWidget(m_modeLabel, 0, 1);
    statsGrid->addWidget(m_filesFoundLabel, 0, 2);
    statsGrid->addWidget(m_chunksLabel, 0, 3);
    statsGrid->addWidget(m_elapsedLabel, 0, 4);

    mainLayout->addWidget(statsGroup);

    // ========== RECOVERY PROGRESS ==========
    auto *progLayout = new QVBoxLayout;
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFixedHeight(24);
    m_progressBar->setStyleSheet(
        "QProgressBar { border: 1px solid #444; border-radius: 4px; text-align: center;"
        "  background: #333; color: #ddd; font-weight: bold; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "  stop:0 #2e7d32, stop:1 #4caf50); border-radius: 3px; }");
    progLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Listo. Configure e inicie la recuperacion.");
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    progLayout->addWidget(m_statusLabel);

    m_progressText = new QLabel;
    m_progressText->setStyleSheet("color: #888;");
    progLayout->addWidget(m_progressText);

    mainLayout->addLayout(progLayout);

    // ========== TABS ==========
    auto *tabs = new QTabWidget;

    // Log tab
    m_logView = new QListView;
    m_logView->setModel(m_logModel);
    m_logView->setAlternatingRowColors(true);
    m_logView->setFont(QFont("Monospace", 9));
    m_logView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tabs->addTab(m_logView, "Eventos");

    // Summary table
    m_summaryModel = new QStandardItemModel(0, 2, this);
    m_summaryModel->setHorizontalHeaderLabels({"Tipo", "Archivos"});
    m_summaryTable = new QTableView;
    m_summaryTable->setModel(m_summaryModel);
    m_summaryTable->horizontalHeader()->setStretchLastSection(true);
    m_summaryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(m_summaryTable, "Resumen");

    // File tree
    m_fileModel = new QStandardItemModel(this);
    m_fileModel->setHorizontalHeaderLabels({"Archivos recuperados"});
    m_fileTree = new QTreeView;
    m_fileTree->setModel(m_fileModel);
    m_fileTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(m_fileTree, "Archivos");

    mainLayout->addWidget(tabs, 1);

    setupStatusBar();
}

void MainWindow::setupToolbar() {
    auto *toolbar = addToolBar("Control");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_settingsBtn = toolbar->addAction("Configurar");
    connect(m_settingsBtn, &QAction::triggered, this, &MainWindow::showSettings);

    toolbar->addSeparator();

    m_startBtn = new QPushButton("Iniciar");
    m_startBtn->setStyleSheet(
        "QPushButton { background-color: #2e7d32; color: white; padding: 6px 16px;"
        "  border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #388e3c; }"
        "QPushButton:disabled { background-color: #555; color: #888; }"
    );
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::showSettings);
    toolbar->addWidget(m_startBtn);

    m_pauseBtn = new QPushButton("Pausar");
    m_pauseBtn->setStyleSheet(
        "QPushButton { background-color: #e65100; color: white; padding: 6px 16px;"
        "  border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #ef6c00; }"
        "QPushButton:disabled { background-color: #555; color: #888; }"
    );
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onPause);
    toolbar->addWidget(m_pauseBtn);

    m_resumeBtn = new QPushButton("Reanudar");
    m_resumeBtn->setStyleSheet(
        "QPushButton { background-color: #1565c0; color: white; padding: 6px 16px;"
        "  border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #1976d2; }"
        "QPushButton:disabled { background-color: #555; color: #888; }"
    );
    connect(m_resumeBtn, &QPushButton::clicked, this, &MainWindow::onResume);
    toolbar->addWidget(m_resumeBtn);

    m_cancelBtn = new QPushButton("Cancelar");
    m_cancelBtn->setStyleSheet(
        "QPushButton { background-color: #b71c1c; color: white; padding: 6px 16px;"
        "  border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #c62828; }"
        "QPushButton:disabled { background-color: #555; color: #888; }"
    );
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancel);
    toolbar->addWidget(m_cancelBtn);
}

void MainWindow::setupStatusBar() {
    statusBar()->showMessage("Listo");
}

void MainWindow::showSettings() {
    if (m_engine->state() == RecoveryEngine::Running ||
        m_engine->state() == RecoveryEngine::Pausing) return;

    SettingsDialog dialog(this);

    StateManager sm;
    if (sm.hasSavedState() && m_engine->state() == RecoveryEngine::Idle) {
        RecoveryState saved = sm.loadState();
        if (saved.isValid())
            dialog.setState(saved);
    }

    connect(&dialog, &SettingsDialog::recoveryRequested,
            this, &MainWindow::onStartRecovery);
    dialog.exec();
}

void MainWindow::onStartRecovery(const RecoveryState &state) {
    m_currentState = state;

    // Update disk info
    QString diskInfo = QString("Particion: %1  |  Capacidad: %2")
        .arg(state.partition, SettingsDialog::bytesToString(state.totalSize));
    if (!state.diskModel.isEmpty())
        diskInfo += "  |  " + state.diskModel;
    m_diskLabel->setText(diskInfo);

    m_diskBar->setValue(100);
    m_diskBar->setFormat(SettingsDialog::bytesToString(state.totalSize));

    m_partitionLabel->setText("Particion: " + state.partition);
    m_modeLabel->setText("Modo: " + QString(state.reportOnly ? "Solo reporte" : "Completo"));

    m_logModel->clear();
    m_summaryModel->removeRows(0, m_summaryModel->rowCount());
    m_fileModel->clear();
    m_fileModel->setHorizontalHeaderLabels({"Archivos recuperados"});
    m_progressBar->setValue(0);
    m_filesFoundLabel->setText("Archivos: 0");
    m_chunksLabel->setText("Chunks: 0");
    m_elapsedLabel->setText("Tiempo: 0s");
    m_statusLabel->setText("Iniciando recuperacion...");

    m_engine->startRecovery(state);
}

void MainWindow::onPause() {
    m_engine->pauseRecovery();
}

void MainWindow::onResume() {
    m_engine->resumeRecovery();
}

void MainWindow::onCancel() {
    m_engine->cancelRecovery();
}

void MainWindow::onEngineStateChanged(RecoveryEngine::State state) {
    updateButtonStates();

    switch (state) {
    case RecoveryEngine::Running:
        m_statusLabel->setText("Recuperando...");
        statusBar()->showMessage("Recuperacion en curso");
        break;
    case RecoveryEngine::Paused:
        m_statusLabel->setText("PAUSADO - Puede cerrar y reanudar luego");
        statusBar()->showMessage("Recuperacion pausada");
        break;
    case RecoveryEngine::Completed:
        m_statusLabel->setText("COMPLETADO - Todos los chunks procesados");
        statusBar()->showMessage("Recuperacion finalizada exitosamente");
        break;
    case RecoveryEngine::Error:
        m_statusLabel->setText("ERROR - Revise el log para mas detalles");
        statusBar()->showMessage("Error durante la recuperacion");
        break;
    case RecoveryEngine::Idle:
        m_statusLabel->setText("Listo");
        statusBar()->showMessage("Listo");
        break;
    default:
        break;
    }
}

void MainWindow::updateButtonStates() {
    auto s = m_engine->state();
    m_startBtn->setEnabled(s == RecoveryEngine::Idle);
    m_pauseBtn->setEnabled(s == RecoveryEngine::Running);
    m_resumeBtn->setEnabled(s == RecoveryEngine::Paused);
    m_cancelBtn->setEnabled(s == RecoveryEngine::Running ||
                            s == RecoveryEngine::Paused ||
                            s == RecoveryEngine::Pausing);

    StateManager sm;
    bool hasSaved = sm.hasSavedState();
    if (s == RecoveryEngine::Idle) {
        m_resumeBtn->setEnabled(hasSaved);
    }
}

void MainWindow::onProgressUpdated(int percent, qint64 offset, qint64 total) {
    m_progressBar->setValue(percent);

    double offsetGb = offset / 1e9;
    double totalGb = total / 1e9;
    m_progressText->setText(QString("%1 GB de %2 GB (%3%) - Archivos encontrados: %4")
                                .arg(offsetGb, 0, 'f', 1)
                                .arg(totalGb, 0, 'f', 1)
                                .arg(percent)
                                .arg(m_engine->filesFound()));
}

void MainWindow::onFileRecovered(const QString &path, const QString &type) {
    m_filesFoundLabel->setText(QString("Archivos: %1").arg(m_engine->filesFound()));
    updateSummary();
}

void MainWindow::onChunkStarted(int chunk, qint64 offset) {
    m_chunksLabel->setText(QString("Chunk: %1").arg(chunk));
    double offsetMb = offset / 1e6;
    m_statusLabel->setText(QString("Procesando chunk %1 (%2 MB)...")
                               .arg(chunk)
                               .arg(offsetMb, 0, 'f', 0));
}

void MainWindow::onChunkFinished(int chunk, int filesInChunk) {
    m_chunksLabel->setText(QString("Chunks: %1").arg(chunk + 1));
    m_statusLabel->setText(QString("Chunk %1 completado - %2 archivos. Continuando...")
                               .arg(chunk).arg(filesInChunk));
    updateSummary();
}

void MainWindow::onFinished(bool success) {
    if (success) {
        m_statusLabel->setText("Recuperacion completada exitosamente");
        m_progressBar->setValue(100);
    }
    updateSummary();
    updateButtonStates();
}

void MainWindow::onLogEntry(LogEntry::Level, const QString &) {
}

void MainWindow::scrollToLastLog() {
    m_logView->scrollToBottom();
}

void MainWindow::updateSummary() {
    QJsonObject byType = m_engine->recoveryState().filesByType;
    m_summaryModel->removeRows(0, m_summaryModel->rowCount());

    int total = 0;
    QStringList categories = {"Word", "Excel", "PowerPoint", "PDF", "RTF", "Texto", "Otros"};
    for (const auto &cat : categories) {
        int count = byType[cat].toInt(0);
        if (count > 0) {
            auto *typeItem = new QStandardItem(cat);
            auto *countItem = new QStandardItem(QString::number(count));
            m_summaryModel->appendRow({typeItem, countItem});
            total += count;
        }
    }

    auto *sepItem = new QStandardItem("---");
    auto *sepCount = new QStandardItem("---");
    m_summaryModel->appendRow({sepItem, sepCount});

    auto *totalItem = new QStandardItem("TOTAL");
    totalItem->setFont(QFont(totalItem->font().family(), -1, QFont::Bold));
    auto *totalCount = new QStandardItem(QString::number(total));
    totalCount->setFont(QFont(totalCount->font().family(), -1, QFont::Bold));
    m_summaryModel->appendRow({totalItem, totalCount});

    // Update file tree
    QString outputDir = m_engine->recoveryState().outputDir;
    m_fileModel->clear();
    if (!outputDir.isEmpty() && QDir(outputDir).exists()) {
        auto *rootItem = m_fileModel->invisibleRootItem();
        QDirIterator it(outputDir, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString dirPath = it.next();
            QDir dir(dirPath);
            int count = dir.entryList(QDir::Files).size();
            if (count > 0) {
                auto *dirItem = new QStandardItem(dir.dirName() + " (" + QString::number(count) + ")");
                rootItem->appendRow(dirItem);
            }
        }
    }
}
