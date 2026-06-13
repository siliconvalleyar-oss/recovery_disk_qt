# Recovery Qt - Aplicación Gráfica de Recuperación de Discos

## Descripción
Aplicación Qt5 C++ que integra todas las técnicas de recuperación de documentos desde particiones dañadas/formateadas. Combina foremost, photorec, dd+strings, compresión ZIP, generación de reportes, pausa/reanudación entre reinicios y monitoreo en tiempo real.

## Compilación Rápida
```bash
cd recovery_qt && mkdir -p build && cd build
cmake .. && make -j$(nproc)
sudo ./recovery_qt
```

## Dependencias
```bash
sudo apt install qtbase5-dev qttools5-dev foremost testdisk pv smartmontools cmake g++
```

## Arquitectura
```
recovery_qt/
├── CMakeLists.txt
├── .opencode/skills/recovery-qt.md
└── src/
    ├── main.cpp              -- Entry point, tema oscuro
    ├── mainwindow.h/.cpp     -- UI principal con pestañas, progreso, SMART
    ├── settingsdialog.h/.cpp -- Config: árbol de discos, SMART, modos, tipos
    ├── recoveryengine.h/.cpp -- Motor con foremost + photorec + strings + ZIP
    ├── statemanager.h/.cpp   -- Persistencia JSON para pausa/reanudación
    └── logmodel.h/.cpp       -- Modelo de log coloreado en tiempo real
```

## Funcionalidades

### Selección de Disco
- Árbol con todos los discos y sus particiones (modelo, capacidad, sistema de archivos)
- Barra de capacidad con codificación de color (azul=NTFS, verde=ext4, rojo=desconocido)
- **SMART Health**: Muestra salud del disco vía smartctl
- **Espacio libre**: Verifica espacio disponible antes de iniciar

### Modos de Recuperación
| Modo | Descripción |
|------|-------------|
| **Solo reporte** | Escaneo rápido con foremost + strings, genera reporte sin extraer |
| **Recuperación completa** | Extracción completa por chunks + foremost + photorec (deep scan opcional) + strings |

### Herramientas de Recuperación
| Herramienta | Propósito |
|-------------|-----------|
| **foremost** | Recuperación por cabeceras de archivo (rápido) |
| **photorec** | Recuperación por estructura de archivos (deep scan) |
| **dd + strings** | Extracción de cadenas de texto legibles (>50 chars) |

### Tipos de Archivo Soportados
- **Word**: .doc .docx .dot .dotx .docm
- **Excel**: .xls .xlsx .xlsm .xlsb .xltx .xltm .csv
- **PowerPoint**: .ppt .pptx .pps .ppsx .pptm
- **PDF**: .pdf
- **RTF**: .rtf
- **Texto**: .txt .text .log .md

### Opciones Avanzadas
- **Excluir Windows** — Filtra .dll, .exe, .sys, y archivos < 1KB
- **Búsqueda profunda** — Ejecuta photorec además de foremost
- **Extraer strings** — Busca cadenas de texto en la partición
- **Organizar por tipo** — Clasifica en carpetas Word/Excel/PDF/etc
- **Comprimir ZIP** — Empaqueta resultados al finalizar

### Pausa / Reanudación (entre reinicios)
- Procesa el disco en chunks configurables (default 500 MB)
- Estado guardado en `~/.config/recovery_qt/state.json`
- Reanuda desde el chunk exacto donde se quedó
- Incluye: offset, chunk, archivos encontrados, tiempo transcurrido

### Monitoreo en Tiempo Real
- **Eventos** — Log coloreado: INFO=azul, OK=verde, WARN=amarillo, ERROR=rojo
- **Resumen** — Archivos por tipo con total acumulado
- **Archivos** — Árbol de directorios de archivos recuperados
- **SMART Health** — Estado de salud del disco detectado
- Progreso con porcentaje y GB procesados

### Reportes
- **Reporte de texto** — Genera reporte detallado al finalizar
- **Log a archivo** — Guarda log completo en el directorio de salida
- **Resumen por tipo** — Conteo de documentos recuperados

## Archivo de Estado
Ubicación: `~/.config/recovery_qt/state.json`
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
  "use_strings": false,
  "compress_zip": false,
  "disk_model": "Samsung SSD 860 EVO",
  "smart_health": "PASSED"
}
```
