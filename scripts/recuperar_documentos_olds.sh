#!/bin/bash
# ================================================================
# Recuperación Inteligente de Documentos - partición Windows
# Busca: .doc .docx .xls .xlsx .ppt .pptx .pdf .rtf .txt .csv
# Excluye archivos de sistema Windows automáticamente
# ================================================================

set -euo pipefail

PART="${1:-/dev/sdd3}"
BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
RECUPERADOS="$BASE_DIR/recuperados/$(date '+%Y%m%d_%H%M%S')"
LOGS="$BASE_DIR/logs"
LOGFILE="$LOGS/recovery_$(date '+%Y%m%d_%H%M%S').log"
REPORT="$LOGS/reporte_$(date '+%Y%m%d_%H%M%S').txt"
USER_OWNER="optimus:optimus"

ROJO='\033[0;31m'; VERDE='\033[0;32m'; AMARILLO='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${CYAN}[INFO]${NC}  $*" | tee -a "$LOGFILE"; }
ok()    { echo -e "${VERDE}[OK]${NC}    $*" | tee -a "$LOGFILE"; }
warn()  { echo -e "${AMARILLO}[WARN]${NC}  $*" | tee -a "$LOGFILE"; }
error() { echo -e "${ROJO}[ERROR]${NC} $*" | tee -a "$LOGFILE"; }

trap 'warn "Script interrumpido"; exit 1' INT TERM

# ----- Verificar root -----
if [ "$(id -u)" -ne 0 ]; then
    echo -e "${ROJO}[ERROR] Ejecutar con sudo: sudo $0 [dispositivo]${NC}"
    exit 1
fi

# ----- Verificar partición -----
if [ ! -b "$PART" ]; then
    error "Particion $PART no existe. Particiones disponibles:"
    lsblk -o NAME,SIZE,FSTYPE,LABEL 2>/dev/null | head -30
    exit 1
fi

# ----- Instalar dependencias -----
install_deps() {
    local missing=()
    for pkg in foremost testdisk; do
        command -v "${pkg}" &>/dev/null || missing+=("$pkg")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        info "Instalando dependencias faltantes: ${missing[*]}"
        apt update -qq && apt install -y -qq "${missing[@]}" 2>/dev/null || {
            warn "No se pudieron instalar algunos paquetes"
        }
    fi
    for pkg in "${missing[@]}"; do
        command -v "$pkg" &>/dev/null && ok "$pkg instalado" || warn "$pkg NO disponible"
    done
}

# ----- Modo solo reporte (rápido) -----
modo_reporte() {
    info "MODO REPORTE - Buscando documentos recuperables en $PART ..."
    local tmpdir=$(mktemp -d)
    
    # foremost modo quiet - solo lista lo que encuentra sin extraer
    if command -v foremost &>/dev/null; then
        info "Usando foremost para escaneo rápido (solo cabeceras)..."
        foremost -t doc,docx,xls,xlsx,ppt,pptx,pdf,rtf,txt,csv -i "$PART" -o "$tmpdir/foremost" -q -v 2>&1 >> "$LOGFILE" || true
        
        if [ -f "$tmpdir/foremost/audit.txt" ]; then
            cp "$tmpdir/foremost/audit.txt" "$RECUPERADOS/foremost_audit.txt"
        fi
    fi
    
    # photorec modo list (sin extraer)
    if command -v photorec &>/dev/null; then
        info "Usando photorec para análisis de estructura de archivos..."
        local prdir="$tmpdir/photorec_scan"
        mkdir -p "$prdir"
        photorec /log /d "$prdir" /cmd "$PART" fileopt,everything,disable,doc,enable,zip,enable,txt,enable,search 2>&1 >> "$LOGFILE" || true
        if ls "$prdir"/*.doc "$prdir"/*.zip "$prdir"/*.txt &>/dev/null 2>&1; then
            cp -r "$prdir"/* "$RECUPERADOS/photorec_scan/" 2>/dev/null || true
        fi
    fi

    # dd + strings para buscar textos legibles
    info "Buscando cadenas de texto recuperables con strings..."
    local strdir="$RECUPERADOS/strings"
    mkdir -p "$strdir"
    dd if="$PART" bs=1M 2>/dev/null | strings -n 50 > "$strdir/todas_cadenas.txt" 2>/dev/null || true

    if [ -f "$strdir/todas_cadenas.txt" ]; then
        # Filtrar textos con contenido de documento (títulos, párrafos)
        grep -i -E '(\.doc|\.docx|\.xls|\.xlsx|\.pdf|documento|informe|report|tesis|carta|contrato|factura|presupuesto)' \
            "$strdir/todas_cadenas.txt" 2>/dev/null | head -200 > "$strdir/documentos_referencia.txt" || true
    fi

    rm -rf "$tmpdir"
    
    # Generar reporte
    generar_reporte_rapido
}

# ----- Reporte rápido -----
generar_reporte_rapido() {
    info "Generando reporte de archivos recuperables..."
    
    {
        echo "============================================"
        echo "  REPORTE DE ARCHIVOS RECUPERABLES"
        echo "  Particion: $PART"
        echo "  Fecha:     $(date)"
        echo "============================================"
        echo ""
        
        # foremost audit
        if [ -f "$RECUPERADOS/foremost_audit.txt" ]; then
            echo "--- ARCHIVOS DETECTADOS POR FOREMOST ---"
            cat "$RECUPERADOS/foremost_audit.txt"
            echo ""
        fi
        
        # photorec resultados
        local prcount=0
        if [ -d "$RECUPERADOS/photorec_scan" ]; then
            prcount=$(find "$RECUPERADOS/photorec_scan" -type f 2>/dev/null | wc -l)
            echo "--- ARCHIVOS DETECTADOS POR PHOTOREC: $prcount ---"
            echo ""
        fi
        
        # strings - nombres de documentos potenciales
        if [ -f "$RECUPERADOS/strings/documentos_referencia.txt" ] && [ -s "$RECUPERADOS/strings/documentos_referencia.txt" ]; then
            echo "--- POSIBLES NOMBRES DE DOCUMENTOS ENCONTRADOS ---"
            head -100 "$RECUPERADOS/strings/documentos_referencia.txt"
            echo ""
        fi
        
        echo "============================================"
        echo "  RESUMEN"
        echo "============================================"
        echo ""
        echo "  Particion: $PART"
        echo "  Tamaño:    $(lsblk -no SIZE "$PART" 2>/dev/null || echo '?')"
        echo ""
        echo "  Para recuperar TODOS los documentos ejecute:"
        echo "    sudo $0 $PART completa"
        echo ""
        echo "  Reporte generado en: $REPORT"
        echo "============================================"
    } > "$REPORT"
    
    cat "$REPORT"
    ok "Reporte generado: $REPORT"
}

# ----- Recuperación completa -----
recuperacion_completa() {
    info "RECUPERACION COMPLETA - Extrayendo documentos de $PART ..."

    # 1. foremost
    if command -v foremost &>/dev/null; then
        local fdir="$RECUPERADOS/foremost"
        info "foremost: recuperando documentos ..."
        foremost -t doc,docx,xls,xlsx,ppt,pptx,pdf,rtf,txt,csv -i "$PART" -o "$fdir" -v -q 2>&1 >> "$LOGFILE" || true
        local fcount=$(find "$fdir" -type f ! -name "*.txt" 2>/dev/null | wc -l)
        ok "foremost: $fcount archivos recuperados"
    fi
    
    # 2. photorec (con filtro de documentos)
    if command -v photorec &>/dev/null; then
        local prdir="$RECUPERADOS/photorec"
        mkdir -p "$prdir"
        info "photorec: recuperando documentos (doc, xls, zip=[docx,xlsx]) ..."
        photorec /log /d "$prdir" /cmd "$PART" fileopt,everything,disable,doc,enable,zip,enable,txt,enable,search 2>&1 >> "$LOGFILE" || true
        
        # Separar zip que contienen docx/xlsx
        if ls "$prdir"/*.zip &>/dev/null 2>&1; then
            local zdir="$RECUPERADOS/photorec_docx"
            mkdir -p "$zdir"
            for z in "$prdir"/*.zip; do
                if unzip -l "$z" 2>/dev/null | grep -qiE '\.(docx|xlsx|pptx)$'; then
                    mv "$z" "$zdir/" 2>/dev/null || true
                fi
            done
            prcount=$(find "$prdir" "$zdir" -type f 2>/dev/null | wc -l)
        else
            prcount=$(find "$prdir" -type f 2>/dev/null | wc -l)
        fi
        ok "photorec: $prcount archivos recuperados"
    fi
    
    # 3. Filtrar archivos de sistema Windows
    filter_windows
    
    # 4. Organizar por tipo
    organizar_archivos
    
    # 5. Reporte final
    generar_reporte_final
}

# ----- Filtrar basura Windows -----
filter_windows() {
    info "Filtrando archivos de sistema Windows y falsos positivos..."
    local deleted=0

    for dir in "$RECUPERADOS/foremost" "$RECUPERADOS/photorec" "$RECUPERADOS/photorec_docx"; do
        [ ! -d "$dir" ] && continue
        # Eliminar archivos de sistema
        for p in "*.dll" "*.exe" "*.sys" "*.drv" "*.cpl" "*.ocx" "*.com" "*.scr" "*.msi" "*.msp"; do
            find "$dir" -name "$p" -delete 2>/dev/null && ((deleted++)) || true
        done
        # Eliminar archivos de menos de 1KB (falsos positivos)
        find "$dir" -type f -size -1k -delete 2>/dev/null && ((deleted++)) || true
    done

    ok "Filtrado: $deleted archivos de sistema/basura eliminados"
}

# ----- Organizar archivos por tipo -----
organizar_archivos() {
    local org="$RECUPERADOS/documentos"
    info "Organizando archivos en $org ..."
    
    declare -A cats=(
        [Word]="doc docx dot dotx docm"
        [Excel]="xls xlsx xlsm xlsb xltx xltm csv"
        [PowerPoint]="ppt pptx pps ppsx pptm"
        [PDF]="pdf"
        [RTF]="rtf"
        [Texto]="txt text log md"
    )
    
    for cat in "${!cats[@]}"; do
        local d="$org/$cat"
        mkdir -p "$d"
        local exts="${cats[$cat]}"
        for ext in $exts; do
            find "$RECUPERADOS/foremost" "$RECUPERADOS/photorec" "$RECUPERADOS/photorec_docx" -name "*.$ext" -exec cp {} "$d/" \; 2>/dev/null || true
        done
        local cnt=$(find "$d" -type f 2>/dev/null | wc -l)
        [ "$cnt" -gt 0 ] && ok "  $cat: $cnt archivos"
    done
    
    # Consolidar conteo total
    local total=$(find "$org" -type f 2>/dev/null | wc -l)
    ok "Total organizados: $total archivos en $org"
}

# ----- Reporte final -----
generar_reporte_final() {
    local org="$RECUPERADOS/documentos"
    
    {
        echo "============================================"
        echo "  REPORTE FINAL DE RECUPERACION"
        echo "  Particion: $PART"
        echo "  Fecha:     $(date)"
        echo "============================================"
        echo ""
        echo "  Directorio de salida: $RECUPERADOS"
        echo "  Log:                 $LOGFILE"
        echo "  Documentos:          $org"
        echo ""
        echo "--- ARCHIVOS POR TIPO ---"
        echo ""
        
        local total=0
        if [ -d "$org" ]; then
            for catdir in "$org"/*/; do
                [ ! -d "$catdir" ] && continue
                local name=$(basename "$catdir")
                local cnt=$(find "$catdir" -type f 2>/dev/null | wc -l)
                echo "  $name: $cnt archivos"
                total=$((total + cnt))
            done
        else
            echo "  No se encontraron archivos recuperados."
        fi
        
        echo ""
        echo "  TOTAL: $total documentos recuperados"
        echo ""
        
        if command -v foremost &>/dev/null && [ -f "$RECUPERADOS/foremost/audit.txt" ]; then
            echo "--- DETALLE FOREMOST ---"
            grep -E '(doc|xls|ppt|pdf|rtf|txt)' "$RECUPERADOS/foremost/audit.txt" 2>/dev/null | head -50
            echo ""
        fi
        
        echo "============================================"
        echo "  RECUPERACION COMPLETADA"
        echo "============================================"
    } > "$REPORT"
    
    cat "$REPORT"
}

# ===== MAIN =====
mkdir -p "$RECUPERADOS" "$RECUPERADOS/strings" "$RECUPERADOS/photorec_scan" "$LOGS" 2>/dev/null

echo -e "${CYAN}"
echo "============================================="
echo "  RECUPERACION INTELIGENTE DE DOCUMENTOS"
echo "  Particion: $PART"
echo "  Tipos: doc docx xls xlsx ppt pptx pdf rtf txt csv"
echo "============================================="
echo -e "${NC}"

install_deps

if [ "${2:-}" = "completa" ]; then
    # ---- RECUPERACION COMPLETA ----
    info "Iniciando recuperacion completa..."
    recuperacion_completa
else
    # ---- MODO REPORTE (default) ----
    info "MODO REPORTE - Solo se analiza la particion (sin extraer archivos)"
    info "Para extraer archivos ejecute: sudo $0 $PART completa"
    echo ""
    modo_reporte
fi

# Restaurar ownership al usuario
chown -R "$USER_OWNER" "$RECUPERADOS" "$LOGS" 2>/dev/null || true

echo ""
echo -e "${VERDE}============================================${NC}"
echo -e "${VERDE}  Recuperados: $RECUPERADOS${NC}"
echo -e "${VERDE}  Logs:        $LOGS${NC}"
echo -e "${VERDE}  Reporte:     $REPORT${NC}"
echo -e "${VERDE}============================================${NC}"
