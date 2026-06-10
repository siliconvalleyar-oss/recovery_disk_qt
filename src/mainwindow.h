#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QListView>
#include <QTableView>
#include <QTreeView>
#include <QStandardItemModel>
#include <QSplitter>
#include <QTextEdit>
#include "recoveryengine.h"
#include "logmodel.h"
#include "settingsdialog.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void showSettings();
    void onStartRecovery(const RecoveryState &state);
    void onPause();
    void onResume();
    void onCancel();
    void onEngineStateChanged(RecoveryEngine::State state);
    void onProgressUpdated(int percent, qint64 offset, qint64 total);
    void onFileRecovered(const QString &path, const QString &type);
    void onChunkStarted(int chunk, qint64 offset);
    void onChunkFinished(int chunk, int filesInChunk);
    void onFinished(bool success);
    void onLogEntry(LogEntry::Level level, const QString &message);
    void scrollToLastLog();

private:
    void setupUi();
    void setupToolbar();
    void setupCentralArea();
    void setupStatusBar();
    void updateButtonStates();
    void updateSummary();

    RecoveryEngine *m_engine;
    LogModel *m_logModel;
    RecoveryState m_currentState;

    // Toolbar buttons
    QAction *m_settingsBtn;
    QPushButton *m_startBtn;
    QPushButton *m_pauseBtn;
    QPushButton *m_resumeBtn;
    QPushButton *m_cancelBtn;

    // Progress - recovery
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
    QLabel *m_progressText;

    // Progress - disk info
    QProgressBar *m_diskBar;
    QLabel *m_diskLabel;

    // Stats
    QLabel *m_filesFoundLabel;
    QLabel *m_chunksLabel;
    QLabel *m_elapsedLabel;
    QLabel *m_partitionLabel;
    QLabel *m_modeLabel;

    // Log
    QListView *m_logView;

    // Summary table
    QTableView *m_summaryTable;
    QStandardItemModel *m_summaryModel;

    // File list
    QTreeView *m_fileTree;
    QStandardItemModel *m_fileModel;
};

#endif
