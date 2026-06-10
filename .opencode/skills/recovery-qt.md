# Recovery Qt - GUI Application

## Description
Qt5 C++ application for recovering documents from damaged/formatted Windows partitions. Supports pause/resume across reboots via chunk-based processing with state persistence.

## Quick Start
```bash
cd recovery_qt
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo ./recovery_qt
```

## Dependencies
```bash
sudo apt install qtbase5-dev qttools5-dev foremost testdisk cmake g++
```

## Architecture
```
recovery_qt/
├── CMakeLists.txt
├── .gitignore
├── .opencode/skills/recovery-qt.md
└── src/
    ├── main.cpp              -- Entry point, dark theme
    ├── mainwindow.h/.cpp     -- Main UI: toolbar, progress, tabs
    ├── settingsdialog.h/.cpp -- Config: disk tree, modes, file types
    ├── recoveryengine.h/.cpp -- Chunk-based recovery with QProcess
    ├── statemanager.h/.cpp   -- JSON state persistence (pause/resume)
    └── logmodel.h/.cpp       -- Real-time colored log model
```

## Features

### Disk Selection
- **Tree view** shows all connected disks and their partitions
- Disks shown with model name, capacity, and partition layout
- Select any partition to recover from
- Visual capacity bar with color-coded filesystem type

### Recovery Modes
| Mode | Description |
|------|-------------|
| **Solo reporte** | Quick scan with `foremost -q`, generates report without extracting files |
| **Recuperacion completa** | Full chunk-based recovery extracting all found documents |

### File Type Filtering
Checkboxes for: .doc .docx .xls .xlsx .ppt .pptx .pdf .rtf .txt .csv

### Advanced Options
- **Excluir archivos Windows** — Filters out .dll, .exe, .sys, etc.
- **Busqueda profunda** — Uses both foremost and photorec sequentially
- **Organizar por tipo** — Sorts recovered files into Word/Excel/PDF/etc folders
- **Comprimir en ZIP** — Packages results on completion

### Pause / Resume (across reboot)
- Processes disk in configurable chunks (default 500 MB)
- State saved to `~/.config/recovery_qt/state.json`
- Resume button available even after restarting the computer
- State includes: offset, chunk number, files found, elapsed time

### Real-time Monitoring
- **Eventos tab** — Colored log (INFO=blue, OK=green, WARN=yellow, ERROR=red)
- **Resumen tab** — Files found by type with running total
- **Archivos tab** — Directory tree of recovered files
- Status bar with progress percentage and GB processed

### UI Theme
- Dark theme with custom stylesheet
- Color-coded progress bars (green=ext4, blue=ntfs, red=unknown)
- Responsive layout with scrollable settings

## State File
Location: `~/.config/recovery_qt/state.json`

```json
{
  "version": 2,
  "partition": "/dev/sdd3",
  "total_size": 1000000000000,
  "current_chunk": 5,
  "offset_bytes": 500000000000,
  "status": "paused",
  "files_found": 42,
  "files_by_type": {"Word": 20, "Excel": 15, "PDF": 7},
  "exclude_windows": true,
  "deep_scan": false,
  "organize_by_type": true,
  "report_only": false
}
```

## Build & Install
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```
