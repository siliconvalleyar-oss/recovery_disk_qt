#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QTreeWidget>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include "statemanager.h"

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    RecoveryState getState() const;
    void setState(const RecoveryState &state);

    static QString bytesToString(qint64 bytes);

signals:
    void recoveryRequested(const RecoveryState &state);

private slots:
    void onStartClicked();
    void onResumeClicked();
    void onRefreshDisks();
    void onSelectOutputDir();
    void onTreeItemClicked(QTreeWidgetItem *item, int column);
    void onReportOnlyToggled(bool checked);

private:
    void setupUi();
    void refreshDiskTree();
    QTreeWidgetItem *addDiskItem(const QString &name, const QString &size,
                                  const QString &model, const QString &path);
    QTreeWidgetItem *addPartitionItem(QTreeWidgetItem *diskItem,
                                       const QString &name, const QString &size,
                                       const QString &fstype, const QString &label,
                                       const QString &path, qint64 bytes);

    QTreeWidget *m_diskTree;
    QLabel *m_selectedInfo;
    QProgressBar *m_diskSizeBar;
    QSpinBox *m_chunkSizeSpin;
    QLineEdit *m_outputDirEdit;
    QLineEdit *m_tmpDirEdit;

    // Mode
    QRadioButton *m_radioReport;
    QRadioButton *m_radioFull;

    // File types
    QCheckBox *m_chkDoc;
    QCheckBox *m_chkDocx;
    QCheckBox *m_chkXls;
    QCheckBox *m_chkXlsx;
    QCheckBox *m_chkPpt;
    QCheckBox *m_chkPptx;
    QCheckBox *m_chkPdf;
    QCheckBox *m_chkRtf;
    QCheckBox *m_chkTxt;
    QCheckBox *m_chkCsv;

    // Advanced options
    QCheckBox *m_chkExcludeWindows;
    QCheckBox *m_chkDeepScan;
    QCheckBox *m_chkOrganize;
    QCheckBox *m_chkCompress;

    QPushButton *m_startBtn;
    QPushButton *m_resumeBtn;
};

#endif
