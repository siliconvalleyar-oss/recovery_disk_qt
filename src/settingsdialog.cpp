#include "settingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileInfo>
#include <QTimer>
#include <QHeaderView>
#include <QScrollArea>
#include <QFont>
#include <cmath>

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Configuracion de Recuperacion");
    setMinimumSize(640, 720);
    resize(700, 780);
    setupUi();
    QTimer::singleShot(0, this, &SettingsDialog::refreshDiskTree);
}

QString SettingsDialog::bytesToString(qint64 bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024) return QString::number(kb, 'f', 1) + " KB";
    double mb = kb / 1024.0;
    if (mb < 1024) return QString::number(mb, 'f', 1) + " MB";
    double gb = mb / 1024.0;
    if (gb < 1024) return QString::number(gb, 'f', 1) + " GB";
    return QString::number(gb / 1024.0, 'f', 2) + " TB";
}

void SettingsDialog::setupUi() {
    auto *scrollContent = new QWidget;
    auto *mainLayout = new QVBoxLayout(scrollContent);

    // ========== DISK SELECTION ==========
    auto *diskGroup = new QGroupBox("Discos y particiones detectados");
    auto *diskLayout = new QVBoxLayout(diskGroup);

    m_diskTree = new QTreeWidget;
    m_diskTree->setHeaderLabels({"Dispositivo", "Tamano", "Tipo", "Sistema", "Etiqueta"});
    m_diskTree->setRootIsDecorated(true);
    m_diskTree->setAnimated(true);
    m_diskTree->setIndentation(20);
    m_diskTree->setAlternatingRowColors(true);
    m_diskTree->header()->setStretchLastSection(true);
    m_diskTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_diskTree->setMinimumHeight(220);
    m_diskTree->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_diskTree, &QTreeWidget::itemClicked, this, &SettingsDialog::onTreeItemClicked);

    diskLayout->addWidget(m_diskTree);

    auto *diskCtrlRow = new QHBoxLayout;
    auto *refreshBtn = new QPushButton("Actualizar discos");
    connect(refreshBtn, &QPushButton::clicked, this, &SettingsDialog::onRefreshDisks);
    diskCtrlRow->addWidget(refreshBtn);

    m_selectedInfo = new QLabel("Seleccione una particion");
    m_selectedInfo->setStyleSheet("color: #aaa; font-weight: bold;");
    diskCtrlRow->addWidget(m_selectedInfo, 1);

    diskLayout->addLayout(diskCtrlRow);

    m_diskSizeBar = new QProgressBar;
    m_diskSizeBar->setRange(0, 100);
    m_diskSizeBar->setValue(0);
    m_diskSizeBar->setTextVisible(true);
    m_diskSizeBar->setFixedHeight(18);
    m_diskSizeBar->hide();
    diskLayout->addWidget(m_diskSizeBar);

    mainLayout->addWidget(diskGroup);

    // ========== MODE ==========
    auto *modeGroup = new QGroupBox("Modo de recuperacion");
    auto *modeLayout = new QHBoxLayout(modeGroup);

    m_radioReport = new QRadioButton("Solo reporte (escaneo rapido)");
    m_radioReport->setToolTip("Solo analiza la particion y genera un reporte de archivos recuperables sin extraerlos");
    m_radioFull = new QRadioButton("Recuperacion completa");
    m_radioFull->setChecked(true);
    m_radioFull->setToolTip("Escanea y extrae todos los documentos encontrados");

    connect(m_radioReport, &QRadioButton::toggled, this, &SettingsDialog::onReportOnlyToggled);

    modeLayout->addWidget(m_radioReport);
    modeLayout->addWidget(m_radioFull);
    modeLayout->addStretch();

    mainLayout->addWidget(modeGroup);

    // ========== ADVANCED OPTIONS ==========
    auto *optGroup = new QGroupBox("Opciones avanzadas");
    auto *optGrid = new QGridLayout(optGroup);

    m_chkExcludeWindows = new QCheckBox("Excluir archivos de sistema Windows (.dll, .exe, .sys...)");
    m_chkExcludeWindows->setChecked(true);
    m_chkExcludeWindows->setToolTip("Filtra automaticamente archivos del sistema operativo Windows");
    optGrid->addWidget(m_chkExcludeWindows, 0, 0, 1, 2);

    m_chkDeepScan = new QCheckBox("Busqueda profunda (usa photorec + foremost, mas lento)");
    m_chkDeepScan->setToolTip("Ejecuta ambas herramientas de recuperacion secuencialmente para maxima cobertura");
    optGrid->addWidget(m_chkDeepScan, 1, 0, 1, 2);

    m_chkOrganize = new QCheckBox("Organizar archivos por tipo automaticamente");
    m_chkOrganize->setChecked(true);
    m_chkOrganize->setToolTip("Clasifica los documentos recuperados en carpetas: Word, Excel, PDF...");
    optGrid->addWidget(m_chkOrganize, 2, 0, 1, 2);

    m_chkCompress = new QCheckBox("Comprimir resultados en ZIP al finalizar");
    m_chkCompress->setToolTip("Empaqueta todos los archivos recuperados en un archivo .zip");
    optGrid->addWidget(m_chkCompress, 3, 0, 1, 2);

    mainLayout->addWidget(optGroup);

    // ========== FILE TYPES ==========
    auto *typeGroup = new QGroupBox("Tipos de documento a recuperar");
    auto *typeGrid = new QGridLayout(typeGroup);

    m_chkDoc  = new QCheckBox(".doc");   m_chkDoc->setChecked(true);
    m_chkDocx = new QCheckBox(".docx");  m_chkDocx->setChecked(true);
    m_chkXls  = new QCheckBox(".xls");   m_chkXls->setChecked(true);
    m_chkXlsx = new QCheckBox(".xlsx");  m_chkXlsx->setChecked(true);
    m_chkPpt  = new QCheckBox(".ppt");   m_chkPpt->setChecked(true);
    m_chkPptx = new QCheckBox(".pptx");  m_chkPptx->setChecked(true);
    m_chkPdf  = new QCheckBox(".pdf");   m_chkPdf->setChecked(true);
    m_chkRtf  = new QCheckBox(".rtf");   m_chkRtf->setChecked(true);
    m_chkTxt  = new QCheckBox(".txt");   m_chkTxt->setChecked(true);
    m_chkCsv  = new QCheckBox(".csv");   m_chkCsv->setChecked(true);

    typeGrid->addWidget(m_chkDoc,  0, 0);
    typeGrid->addWidget(m_chkDocx, 0, 1);
    typeGrid->addWidget(m_chkXls,  1, 0);
    typeGrid->addWidget(m_chkXlsx, 1, 1);
    typeGrid->addWidget(m_chkPpt,  2, 0);
    typeGrid->addWidget(m_chkPptx, 2, 1);
    typeGrid->addWidget(m_chkPdf,  3, 0);
    typeGrid->addWidget(m_chkRtf,  3, 1);
    typeGrid->addWidget(m_chkTxt,  4, 0);
    typeGrid->addWidget(m_chkCsv,  4, 1);

    mainLayout->addWidget(typeGroup);

    // ========== DIRECTORIES & CHUNK ==========
    auto *dirGroup = new QGroupBox("Directorios y rendimiento");
    auto *dirForm = new QFormLayout(dirGroup);

    m_outputDirEdit = new QLineEdit(QDir::homePath() + "/recuperados");
    auto *outBtn = new QPushButton("...");
    outBtn->setFixedWidth(30);
    connect(outBtn, &QPushButton::clicked, this, &SettingsDialog::onSelectOutputDir);
    auto *outRow = new QHBoxLayout();
    outRow->addWidget(m_outputDirEdit, 1);
    outRow->addWidget(outBtn);
    dirForm->addRow("Directorio de salida:", outRow);

    m_tmpDirEdit = new QLineEdit("/tmp/recovery_qt");
    dirForm->addRow("Directorio temporal:", m_tmpDirEdit);

    m_chunkSizeSpin = new QSpinBox;
    m_chunkSizeSpin->setRange(50, 10000);
    m_chunkSizeSpin->setValue(500);
    m_chunkSizeSpin->setSuffix(" MB");
    m_chunkSizeSpin->setSingleStep(100);
    m_chunkSizeSpin->setToolTip("Tamano de cada fragmento a procesar. Mayor = mas memoria, menor = mas pausas frecuentes");
    dirForm->addRow("Tamano de chunk:", m_chunkSizeSpin);

    mainLayout->addWidget(dirGroup);

    // ========== BUTTONS ==========
    auto *btnLayout = new QHBoxLayout;

    m_startBtn = new QPushButton("Iniciar Recuperacion");
    m_startBtn->setStyleSheet(
        "QPushButton { background-color: #2e7d32; color: white; padding: 8px 20px;"
        "  border-radius: 4px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background-color: #388e3c; }"
        "QPushButton:disabled { background-color: #555; color: #888; }"
    );
    connect(m_startBtn, &QPushButton::clicked, this, &SettingsDialog::onStartClicked);
    m_startBtn->setEnabled(false);

    m_resumeBtn = new QPushButton("Reanudar (pausada)");
    m_resumeBtn->setStyleSheet(
        "QPushButton { background-color: #e65100; color: white; padding: 8px 20px;"
        "  border-radius: 4px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background-color: #ef6c00; }"
        "QPushButton:disabled { background-color: #555; color: #888; }"
    );
    connect(m_resumeBtn, &QPushButton::clicked, this, &SettingsDialog::onResumeClicked);

    auto *cancelBtn = new QPushButton("Cancelar");
    cancelBtn->setStyleSheet(
        "QPushButton { padding: 8px 20px; border-radius: 4px; font-size: 13px; }"
    );
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    StateManager sm;
    m_resumeBtn->setVisible(sm.hasSavedState());

    btnLayout->addWidget(m_startBtn);
    btnLayout->addWidget(m_resumeBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);

    mainLayout->addLayout(btnLayout);
    mainLayout->addStretch();

    // Scroll area
    auto *scrollArea = new QScrollArea;
    scrollArea->setWidget(scrollContent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

void SettingsDialog::refreshDiskTree() {
    m_diskTree->clear();
    QMap<QString, QTreeWidgetItem*> diskItems;

    QProcess proc;
    proc.start("lsblk", QStringList() << "-o" << "NAME,SIZE,TYPE,FSTYPE,LABEL,MODEL"
                                      << "-J" << "-b" << "-d");
    proc.waitForFinished(5000);
    QByteArray out = proc.readAllStandardOutput();

    // Also get partitions separately with -J output including children
    QProcess procFull;
    procFull.start("lsblk", QStringList() << "-o" << "NAME,SIZE,TYPE,FSTYPE,LABEL"
                                          << "-J" << "-b");
    procFull.waitForFinished(5000);
    QByteArray outFull = procFull.readAllStandardOutput();

    QJsonDocument doc = QJsonDocument::fromJson(out);
    QJsonDocument docFull = QJsonDocument::fromJson(outFull);

    if (doc.isNull() || docFull.isNull()) {
        auto *item = new QTreeWidgetItem(m_diskTree);
        item->setText(0, "No se pudieron detectar discos. Verifique permisos.");
        return;
    }

    QJsonArray disks = doc.object()["blockdevices"].toArray();
    QJsonArray allDevices = docFull.object()["blockdevices"].toArray();

    // Map from name to full device info (with children)
    QMap<QString, QJsonObject> deviceMap;
    std::function<void(const QJsonArray&)> mapDevices = [&](const QJsonArray &arr) {
        for (const auto &val : arr) {
            QJsonObject obj = val.toObject();
            QString name = obj["name"].toString();
            deviceMap[name] = obj;
            QJsonArray children = obj["children"].toArray();
            if (!children.isEmpty())
                mapDevices(children);
        }
    };
    mapDevices(allDevices);

    // Add disks
    for (const auto &val : disks) {
        QJsonObject obj = val.toObject();
        QString name = obj["name"].toString();
        QString type = obj["type"].toString();
        if (type != "disk") continue;

        qint64 size = obj["size"].toVariant().toLongLong();
        QString model = obj["model"].toString().trimmed();
        QString path = "/dev/" + name;

        auto *diskItem = new QTreeWidgetItem(m_diskTree);
        diskItem->setText(0, path);
        diskItem->setText(1, bytesToString(size));
        diskItem->setText(2, "Disco");
        diskItem->setText(3, "");
        diskItem->setText(4, model);
        diskItem->setData(0, Qt::UserRole, path);
        diskItem->setData(0, Qt::UserRole + 1, "disk");
        diskItem->setData(1, Qt::UserRole, size);
        diskItem->setFlags(diskItem->flags() & ~Qt::ItemIsSelectable);

        // Bold for disk names
        QFont diskFont = diskItem->font(0);
        diskFont.setBold(true);
        for (int c = 0; c < 5; c++)
            diskItem->setFont(c, diskFont);

        QString sizeStr = bytesToString(size);
        diskItem->setText(1, sizeStr);
        if (!model.isEmpty())
            diskItem->setText(0, path + "  —  " + model);

        diskItems[name] = diskItem;
        m_diskTree->expandItem(diskItem);

        // Add partitions from the full device map
        if (deviceMap.contains(name)) {
            QJsonArray children = deviceMap[name]["children"].toArray();
            for (const auto &childVal : children) {
                QJsonObject part = childVal.toObject();
                QString pName = part["name"].toString();
                QString pType = part["type"].toString();
                if (pType != "part") continue;

                qint64 pSize = part["size"].toVariant().toLongLong();
                QString pFstype = part["fstype"].toString();
                QString pLabel = part["label"].toString();
                QString pPath = "/dev/" + pName;

                auto *partItem = new QTreeWidgetItem(diskItem);
                partItem->setText(0, pPath);
                partItem->setText(1, bytesToString(pSize));
                partItem->setText(2, "Particion");
                partItem->setText(3, pFstype.isEmpty() ? "desconocido" : pFstype);
                partItem->setText(4, pLabel);
                partItem->setData(0, Qt::UserRole, pPath);
                partItem->setData(0, Qt::UserRole + 1, "part");
                partItem->setData(1, Qt::UserRole, pSize);
                partItem->setData(3, Qt::UserRole, pFstype);
            }
        }
    }
}

void SettingsDialog::onTreeItemClicked(QTreeWidgetItem *item, int) {
    QString type = item->data(0, Qt::UserRole + 1).toString();
    if (type != "part") {
        m_selectedInfo->setText("Seleccione una particion (no un disco completo)");
        m_startBtn->setEnabled(false);
        m_diskSizeBar->hide();
        return;
    }

    QString path = item->data(0, Qt::UserRole).toString();
    qint64 size = item->data(1, Qt::UserRole).toLongLong();
    QString fstype = item->data(3, Qt::UserRole).toString();
    QString label = item->text(4);

    QString info = path + "  |  " + bytesToString(size);
    if (!fstype.isEmpty()) info += "  |  " + fstype;
    if (!label.isEmpty()) info += "  |  " + label;

    m_selectedInfo->setText(info);
    m_startBtn->setEnabled(true);

    // Show disk usage bar (visual representation of disk capacity)
    m_diskSizeBar->setValue(100);
    m_diskSizeBar->setFormat(bytesToString(size));
    m_diskSizeBar->show();

    // Color the bar based on filesystem
    if (fstype == "ntfs")
        m_diskSizeBar->setStyleSheet(
            "QProgressBar { border: 1px solid #444; border-radius: 4px; text-align: center;"
            "  background: #333; color: #ddd; }"
            "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "  stop:0 #1565c0, stop:1 #1976d2); border-radius: 3px; }");
    else if (fstype.isEmpty())
        m_diskSizeBar->setStyleSheet(
            "QProgressBar { border: 1px solid #444; border-radius: 4px; text-align: center;"
            "  background: #333; color: #ddd; }"
            "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "  stop:0 #b71c1c, stop:1 #c62828); border-radius: 3px; }");
    else
        m_diskSizeBar->setStyleSheet(
            "QProgressBar { border: 1px solid #444; border-radius: 4px; text-align: center;"
            "  background: #333; color: #ddd; }"
            "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "  stop:0 #2e7d32, stop:1 #4caf50); border-radius: 3px; }");
}

void SettingsDialog::onReportOnlyToggled(bool checked) {
    m_chunkSizeSpin->setEnabled(!checked);
    if (checked) {
        m_chunkSizeSpin->setToolTip("Deshabilitado en modo solo reporte");
    } else {
        m_chunkSizeSpin->setToolTip("Tamano de cada fragmento a procesar");
    }
}

void SettingsDialog::onRefreshDisks() {
    refreshDiskTree();
}

void SettingsDialog::onSelectOutputDir() {
    QString dir = QFileDialog::getExistingDirectory(this, "Directorio de salida",
                                                      m_outputDirEdit->text());
    if (!dir.isEmpty())
        m_outputDirEdit->setText(dir);
}

void SettingsDialog::onStartClicked() {
    RecoveryState s = getState();
    if (s.partition.isEmpty()) {
        m_selectedInfo->setText("ERROR: Seleccione una particion valida");
        m_selectedInfo->setStyleSheet("color: #ff5252; font-weight: bold;");
        return;
    }
    QFileInfo fi(s.outputDir);
    if (!fi.exists()) QDir().mkpath(s.outputDir);
    QDir().mkpath(s.tmpDir);

    emit recoveryRequested(s);
    accept();
}

void SettingsDialog::onResumeClicked() {
    StateManager sm;
    RecoveryState saved = sm.loadState();
    if (saved.isValid() && saved.status == "paused") {
        setState(saved);
        emit recoveryRequested(saved);
        accept();
    }
}

RecoveryState SettingsDialog::getState() const {
    RecoveryState s;

    // Get selected partition from tree
    auto selected = m_diskTree->selectedItems();
    if (!selected.isEmpty()) {
        QTreeWidgetItem *item = selected.first();
        s.partition = item->data(0, Qt::UserRole).toString();
        s.totalSize = item->data(1, Qt::UserRole).toLongLong();
        s.diskModel = item->text(4);

        // Get disk name from parent (for model)
        if (item->parent()) {
            QString diskModel = item->parent()->text(4);
            if (diskModel.isEmpty())
                diskModel = item->parent()->text(0);
            s.diskModel = diskModel;
        }
    }

    // Mode
    s.reportOnly = m_radioReport->isChecked();

    // Advanced options
    s.excludeWindows = m_chkExcludeWindows->isChecked();
    s.deepScan = m_chkDeepScan->isChecked();
    s.organizeByType = m_chkOrganize->isChecked();

    // Chunk size
    s.chunkSizeMb = m_chunkSizeSpin->value();

    // Directories
    s.outputDir = m_outputDirEdit->text();
    s.tmpDir = m_tmpDirEdit->text();

    // File types
    s.fileTypes.clear();
    if (m_chkDoc->isChecked())  s.fileTypes << "doc";
    if (m_chkDocx->isChecked()) s.fileTypes << "docx";
    if (m_chkXls->isChecked())  s.fileTypes << "xls";
    if (m_chkXlsx->isChecked()) s.fileTypes << "xlsx";
    if (m_chkPpt->isChecked())  s.fileTypes << "ppt";
    if (m_chkPptx->isChecked()) s.fileTypes << "pptx";
    if (m_chkPdf->isChecked())  s.fileTypes << "pdf";
    if (m_chkRtf->isChecked())  s.fileTypes << "rtf";
    if (m_chkTxt->isChecked())  s.fileTypes << "txt";
    if (m_chkCsv->isChecked())  s.fileTypes << "csv";

    return s;
}

void SettingsDialog::setState(const RecoveryState &state) {
    m_radioFull->setChecked(!state.reportOnly);
    m_radioReport->setChecked(state.reportOnly);

    m_chkExcludeWindows->setChecked(state.excludeWindows);
    m_chkDeepScan->setChecked(state.deepScan);
    m_chkOrganize->setChecked(state.organizeByType);

    m_chunkSizeSpin->setValue(state.chunkSizeMb);
    m_outputDirEdit->setText(state.outputDir);
    m_tmpDirEdit->setText(state.tmpDir);
}
