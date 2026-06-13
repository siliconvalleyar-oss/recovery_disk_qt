#!/bin/bash
# ================================================================
# Recuperación Inteligente de Documentos - partición Windows
# Busca: .doc .docx .xls .xlsx .ppt .pptx .pdf .rtf .txt .csv
# Versión interactiva con barra de progreso y logging mejorado
# ================================================================

set -euo pipefail

# ---------- Configuración ----------
PART="${1:-/dev/sdd3}"
BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
RECUPERADOS="$BASE_DIR/recuperados/$(date '+%Y%m%d_%H%M%S')"
LOGS="$BASE_DIR/logs"
LOGFILE="$LOGS/recovery_$(date '+%Y%m%d_%H%M%S').log"
REPORT="$LOGS/reporte_$(date '+%Y%m%d_%H%M%S').txt"
USER_OWNER="optimus:optimus"
TIMEOUT_DD=600        # 10 minutos para dd
TIMEOUT_PHOTOREC=7200 # 2 horas para photorec
TIMEOUT_FOREMOST=3600 # 1 hora para foremost

# Colores y estilos
ROJO='\033[0;31m'; VERDE='\033[0;32m'; AMARILLO='\033[1;33m'; CYAN='\033[0;36m'
MAGENTA='\033[0;35m'; GRIS='\033[0;37m'; NEGRITA='\033[1m'; NC='\033[0m'
BARRA_LEN=50

# Variables de estado
ESTADO_ACTUAL="Inicializando"
PROGRESO_ACTUAL=0
TOTAL_PASOS=0

# ---------- Funciones de logging y UI ----------
info()  { echo -e "${CYAN}[INFO]${NC}  $(date '+%H:%M:%S') $*" | tee -a "$LOGFILE"; }
ok()    { echo -e "${VERDE}[OK]${NC}    $(date '+%H:%M:%S') $*" | tee -a "$LOGFILE"; }
warn()  { echo -e "${AMARILLO}[WARN]${NC}  $(date '+%H:%M:%S') $*" | tee -a "$LOGFILE"; }
error() { echo -e "${ROJO}[ERROR]${NC} $(date '+%H:%M:%S') $*" | tee -a "$LOGFILE"; }
evento() { echo -e "${MAGENTA}[EVENTO]${NC} $(date '+%H:%M:%S') $*" | tee -a "$LOGFILE"; }

# Barra de progreso horizontal
barra_progreso() {
    local porcentaje=$1
    local filled=$(printf "%.0f" $(echo "$porcentaje * $BARRA_LEN / 100" | bc -l 2>/dev/null || echo 0))
    [ "$filled" -gt "$BARRA_LEN" ] && filled=$BARRA_LEN
    local vacio=$((BARRA_LEN - filled))
    printf "\r${CYAN}Progreso:${NC} ["
    printf "%${filled}s" | tr ' ' '='
    printf "%${vacio}s" | tr ' ' ' '
    printf "] %3.0f%%  ${GRIS}%s${NC}" "$porcentaje" "$ESTADO_ACTUAL"
}

# Mostrar estado de la partición
mostrar_estado_particion() {
    clear
    echo -e "${NEGRITA}${CYAN}=============================================${NC}"
    echo -e "${NEGRITA}      RECUPERACIÓN INTELIGENTE DE DOCUMENTOS${NC}"
    echo -e "${CYAN}=============================================${NC}"
    echo -e "${NEGRITA}Particion:${NC} $PART"
    if [ -b "$PART" ]; then
        local size=$(lsblk -no SIZE "$PART" 2>/dev/null || echo "Desconocido")
        local fstype=$(lsblk -no FSTYPE "$PART" 2>/dev/null || echo "Desconocido")
        local label=$(lsblk -no LABEL "$PART" 2>/dev/null || echo "Sin etiqueta")
        echo -e "${NEGRITA}Tamaño:${NC}    $size"
        echo -e "${NEGRITA}Tipo:${NC}      $fstype"
        echo -e "${NEGRITA}Etiqueta:${NC}  $label"

        if command -v smartctl &>/dev/null; then
            evento "Obteniendo salud del disco con smartctl..."
            local salud=$(sudo smartctl -H "$PART" 2>/dev/null | grep -i "SMART overall-health" | awk -F': ' '{print $2}')
            if [ -n "$salud" ]; then
                if [ "$salud" = "PASSED" ]; then
                    echo -e "${NEGRITA}Salud SMART:${NC} ${VERDE}$salud${NC}"
                else
                    echo -e "${NEGRITA}Salud SMART:${NC} ${ROJO}$salud${NC}"
                fi
            else
                echo -e "${NEGRITA}Salud SMART:${NC} ${AMARILLO}No disponible${NC}"
            fi
        else
            echo -e "${NEGRITA}SMART:${NC} ${AMARILLO}smartctl no instalado${NC}"
        fi
    else
        echo -e "${ROJO}ERROR: La partición $PART no existe.${NC}"
    fi
    echo -e "${CYAN}=============================================${NC}"
}

# Mostrar menú principal
mostrar_menu() {
    echo ""
    echo -e "${NEGRITA}${CYAN}===== MENÚ PRINCIPAL =====${NC}"
    echo -e " ${VERDE}1)${NC} Reporte rápido (solo analizar, sin recuperar)"
    echo -e " ${VERDE}2)${NC} Recuperación completa (extraer documentos)"
    echo -e " ${VERDE}3)${NC} Ver log de la última ejecución"
    echo -e " ${VERDE}4)${NC} Salir"
    echo ""
    echo -n "Seleccione una opción [1-4]: "
}

# Ver log (último archivo)
ver_log() {
    local ultimo_log=$(ls -t "$LOGS"/recovery_*.log 2>/dev/null | head -1)
    if [ -z "$ultimo_log" ]; then
        echo -e "${AMARILLO}No hay logs aún.${NC}"
        sleep 2
        return
    fi
    echo -e "${CYAN}Mostrando log: $ultimo_log${NC}"
    echo -e "${AMARILLO}Presione 'q' para salir del visor.${NC}"
    sleep 2
    less -R "$ultimo_log"
}

# ---------- Funciones de recuperación con progreso ----------
# foremost con barra de progreso (estimada)
run_foremost_con_progreso() {
    local outdir="$1"
    local tipo="doc,docx,xls,xlsx,ppt,pptx,pdf,rtf,txt,csv"
    mkdir -p "$outdir"
    evento "Ejecutando foremost en segundo plano para escanear cabeceras..."

    # Obtener tamaño total de la partición en bytes
    local total_bytes=$(blockdev --getsize64 "$PART" 2>/dev/null || echo 0)
    if [ "$total_bytes" -eq 0 ]; then
        warn "No se pudo determinar el tamaño de la partición para el progreso."
        # Ejecutar foremost sin progreso
        timeout "$TIMEOUT_FOREMOST" foremost -t "$tipo" -i "$PART" -o "$outdir" -v -q >> "$LOGFILE" 2>&1 || true
        return
    fi

    # Usar pv para mostrar progreso si está disponible, sino dd status=progress
    if command -v pv &>/dev/null; then
        evento "Usando pv para barra de progreso."
        pv -n -s "$total_bytes" "$PART" | foremost -t "$tipo" -o "$outdir" -v -q 2>> "$LOGFILE" 2>&1 | \
        while read -r p; do
            [ -n "$p" ] && PROGRESO_ACTUAL=$(echo "scale=0; $p * 100 / 1" | bc 2>/dev/null || echo 0)
            barra_progreso "$PROGRESO_ACTUAL"
        done
    else
        evento "pv no instalado; usando dd status=progress (Linux moderno)."
        dd if="$PART" bs=1M status=progress 2>&1 | \
        while read -r line; do
            if [[ "$line" =~ ([0-9]+)\ bytes ]]; then
                local leido=${BASH_REMATCH[1]}
                local porc=$(echo "scale=2; $leido * 100 / $total_bytes" | bc -l 2>/dev/null || echo 0)
                PROGRESO_ACTUAL=$(printf "%.0f" "$porc")
                barra_progreso "$PROGRESO_ACTUAL"
            fi
        done | foremost -t "$tipo" -o "$outdir" -v -q 2>> "$LOGFILE"
    fi
    echo "" # nueva línea después de la barra
    ok "foremost finalizado. Revisar $outdir"
}

# photorec no interactivo con barra de progreso simulada (por pasos)
run_photorec_con_progreso() {
    local outdir="$1"
    mkdir -p "$outdir"
    evento "Preparando entorno para photorec no interactivo..."

    # Crear archivo de respuestas
    local answers=$(mktemp)
    printf "\n\n\nY\n" > "$answers"

    # El progreso de photorec es difícil de medir, simularemos 3 etapas: inicio, escaneo, final
    ESTADO_ACTUAL="Iniciando photorec"
    barra_progreso 5
    sleep 2

    ESTADO_ACTUAL="Escaneando sectores (puede ser muy lento)"
    barra_progreso 10

    # Ejecutar con timeout y redirigir stdin
    timeout "$TIMEOUT_PHOTOREC" \
        photorec /log /d "$outdir" /cmd "$PART" \
        fileopt,everything,disable,doc,enable,zip,enable,txt,enable,search \
        < "$answers" >> "$LOGFILE" 2>&1 &
    local pid=$!

    # Simular progreso mientras photorec corre (cada 30s aumenta 5% hasta 95%)
    local progreso=10
    while kill -0 $pid 2>/dev/null; do
        sleep 30
        progreso=$((progreso + 5))
        [ $progreso -gt 95 ] && progreso=95
        PROGRESO_ACTUAL=$progreso
        barra_progreso "$PROGRESO_ACTUAL"
    done
    wait $pid
    PROGRESO_ACTUAL=100
    barra_progreso 100
    echo ""
    rm -f "$answers"

    # Limpiar códigos de escape del log
    sed -i 's/\x1b\[[0-9;]*[a-zA-Z]//g' "$LOGFILE" 2>/dev/null || true
    ok "photorec completado"
}

# dd + strings con barra de progreso
run_strings_con_progreso() {
    local outfile="$1"
    local total_bytes=$(blockdev --getsize64 "$PART" 2>/dev/null || echo 0)
    evento "Extrayendo cadenas de texto con strings (mínimo 50 caracteres)..."

    if command -v pv &>/dev/null && [ "$total_bytes" -gt 0 ]; then
        pv -n -s "$total_bytes" "$PART" | strings -n 50 > "$outfile" 2>/dev/null | \
        while read -r p; do
            [ -n "$p" ] && PROGRESO_ACTUAL=$(echo "scale=0; $p * 100 / 1" | bc 2>/dev/null || echo 0)
            barra_progreso "$PROGRESO_ACTUAL"
        done
    else
        dd if="$PART" bs=1M status=progress 2>&1 | \
        while read -r line; do
            if [[ "$line" =~ ([0-9]+)\ bytes ]]; then
                local leido=${BASH_REMATCH[1]}
                local porc=$(echo "scale=2; $leido * 100 / $total_bytes" | bc -l 2>/dev/null || echo 0)
                PROGRESO_ACTUAL=$(printf "%.0f" "$porc")
                barra_progreso "$PROGRESO_ACTUAL"
            fi
        done | strings -n 50 > "$outfile"
    fi
    echo ""
    ok "Archivo de cadenas generado: $outfile"
}

# ---------- Modo reporte (rápido) ----------
modo_reporte() {
    clear
    mostrar_estado_particion
    echo -e "${CYAN}--- MODO REPORTE RÁPIDO ---${NC}"
    evento "Iniciando modo reporte en $PART"

    local tmpdir=$(mktemp -d)
    local strdir="$RECUPERADOS/strings"
    mkdir -p "$strdir"

    # 1. foremost rápido sin extracción (solo audit)
    if command -v foremost &>/dev/null; then
        ESTADO_ACTUAL="Escaneando cabeceras con foremost"
        PROGRESO_ACTUAL=0
        barra_progreso 0
        run_foremost_con_progreso "$tmpdir/foremost"
        if [ -f "$tmpdir/foremost/audit.txt" ]; then
            cp "$tmpdir/foremost/audit.txt" "$RECUPERADOS/foremost_audit.txt"
            evento "Foremost audit guardado en $RECUPERADOS/foremost_audit.txt"
        fi
    else
        warn "foremost no instalado. Instálelo con: sudo apt install foremost"
    fi

    # 2. strings para buscar textos
    ESTADO_ACTUAL="Buscando cadenas de texto legibles"
    PROGRESO_ACTUAL=0
    barra_progreso 0
    run_strings_con_progreso "$strdir/todas_cadenas.txt"

    if [ -f "$strdir/todas_cadenas.txt" ]; then
        grep -i -E '(\.doc|\.docx|\.xls|\.xlsx|\.pdf|documento|informe|report|tesis|carta|contrato|factura|presupuesto)' \
            "$strdir/todas_cadenas.txt" 2>/dev/null | head -200 > "$strdir/documentos_referencia.txt"
        evento "Cadenas relevantes extraídas en $strdir/documentos_referencia.txt"
    fi

    rm -rf "$tmpdir"
    generar_reporte_rapido
}

# Generar reporte rápido
generar_reporte_rapido() {
    evento "Generando reporte..."
    {
        echo "============================================"
        echo "  REPORTE DE ARCHIVOS RECUPERABLES"
        echo "  Particion: $PART"
        echo "  Fecha:     $(date)"
        echo "============================================"
        echo ""
        if [ -f "$RECUPERADOS/foremost_audit.txt" ]; then
            echo "--- ARCHIVOS DETECTADOS POR FOREMOST ---"
            cat "$RECUPERADOS/foremost_audit.txt"
            echo ""
        fi
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
        echo "    o seleccione opción 2 en el menú."
        echo ""
        echo "  Reporte generado: $REPORT"
        echo "============================================"
    } > "$REPORT"
    cat "$REPORT"
    ok "Reporte generado: $REPORT"
    echo -e "${AMARILLO}Presione Enter para continuar...${NC}"
    read -r
}

# ---------- Recuperación completa ----------
recuperacion_completa() {
    clear
    mostrar_estado_particion
    echo -e "${CYAN}--- RECUPERACIÓN COMPLETA ---${NC}"
    evento "Iniciando recuperación completa en $PART"

    # Verificar espacio
    local part_size=$(blockdev --getsize64 "$PART" 2>/dev/null || echo 0)
    local free_space=$(df --output=avail "$BASE_DIR" | tail -1)
    if [ "$part_size" -gt 0 ] && [ "$free_space" -lt "$part_size" ]; then
        warn "Espacio libre insuficiente: $(numfmt --to=iec $free_space) libre vs partición $(numfmt --to=iec $part_size)."
        echo -n "¿Continuar de todos modos? (s/N): "
        read -r resp
        [[ ! "$resp" =~ ^[Ss] ]] && { evento "Recuperación cancelada por falta de espacio."; return; }
    fi

    # 1. foremost
    if command -v foremost &>/dev/null; then
        local fdir="$RECUPERADOS/foremost"
        ESTADO_ACTUAL="Recuperando con foremost"
        PROGRESO_ACTUAL=0
        barra_progreso 0
        run_foremost_con_progreso "$fdir"
        local fcount=$(find "$fdir" -type f ! -name "*.txt" 2>/dev/null | wc -l)
        ok "foremost recuperó $fcount archivos"
    fi

    # 2. photorec
    if command -v photorec &>/dev/null; then
        local prdir="$RECUPERADOS/photorec"
        ESTADO_ACTUAL="Recuperando con photorec"
        PROGRESO_ACTUAL=0
        barra_progreso 0
        run_photorec_con_progreso "$prdir"
        # Separar ZIP que contienen docx/xlsx
        if ls "$prdir"/*.zip &>/dev/null 2>&1; then
            local zdir="$RECUPERADOS/photorec_docx"
            mkdir -p "$zdir"
            for z in "$prdir"/*.zip; do
                if unzip -l "$z" 2>/dev/null | grep -qiE '\.(docx|xlsx|pptx)$'; then
                    mv "$z" "$zdir/" 2>/dev/null || true
                fi
            done
        fi
        local prcount=$(find "$RECUPERADOS/photorec" "$RECUPERADOS/photorec_docx" -type f 2>/dev/null | wc -l)
        ok "photorec recuperó $prcount archivos"
    fi

    # 3. Filtrar y organizar
    ESTADO_ACTUAL="Filtrando archivos de sistema"
    PROGRESO_ACTUAL=80
    barra_progreso 80
    filter_windows
    ESTADO_ACTUAL="Organizando por tipo"
    PROGRESO_ACTUAL=90
    barra_progreso 90
    organizar_archivos
    PROGRESO_ACTUAL=100
    barra_progreso 100
    echo ""
    generar_reporte_final
}

# ----- Funciones auxiliares (filter, organizar) sin cambios significativos -----
filter_windows() {
    info "Filtrando archivos de sistema Windows y falsos positivos..."
    local total_deleted=0
    for dir in "$RECUPERADOS/foremost" "$RECUPERADOS/photorec" "$RECUPERADOS/photorec_docx"; do
        [ ! -d "$dir" ] && continue
        for p in "*.dll" "*.exe" "*.sys" "*.drv" "*.cpl" "*.ocx" "*.com" "*.scr" "*.msi" "*.msp"; do
            cnt=$(find "$dir" -name "$p" -type f -delete -print 2>/dev/null | wc -l)
            total_deleted=$((total_deleted + cnt))
        done
        cnt=$(find "$dir" -type f -size -1k -delete -print 2>/dev/null | wc -l)
        total_deleted=$((total_deleted + cnt))
    done
    ok "Filtrado: $total_deleted archivos eliminados"
}

organizar_archivos() {
    local org="$RECUPERADOS/documentos"
    info "Organizando archivos en $org (enlaces duros)..."
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
            find "$RECUPERADOS/foremost" "$RECUPERADOS/photorec" "$RECUPERADOS/photorec_docx" \
                -type f -name "*.$ext" -exec cp -l {} "$d/" \; 2>/dev/null || true
        done
        local cnt=$(find "$d" -type f 2>/dev/null | wc -l)
        [ "$cnt" -gt 0 ] && ok "  $cat: $cnt archivos"
    done
    local total=$(find "$org" -type f 2>/dev/null | wc -l)
    ok "Total organizados: $total archivos en $org"
}

generar_reporte_final() {
    local org="$RECUPERADOS/documentos"
    {
        echo "============================================"
        echo "  REPORTE FINAL DE RECUPERACION"
        echo "  Particion: $PART"
        echo "  Fecha:     $(date)"
        echo "============================================"
        echo ""
        echo "  Directorio: $RECUPERADOS"
        echo "  Log:        $LOGFILE"
        echo "  Documentos: $org"
        echo ""
        echo "--- ARCHIVOS POR TIPO ---"
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
        echo "============================================"
    } > "$REPORT"
    cat "$REPORT"
    ok "Reporte final: $REPORT"
    echo -e "${AMARILLO}Presione Enter para continuar...${NC}"
    read -r
}

# ---------- Instalación de dependencias ----------
install_deps() {
    local missing=()
    for pkg in foremost testdisk pv smartctl; do
        if ! command -v "$pkg" &>/dev/null; then
            missing+=("$pkg")
        fi
    done
    if [ ${#missing[@]} -gt 0 ]; then
        info "Instalando dependencias faltantes: ${missing[*]}"
        apt update -qq && apt install -y -qq "${missing[@]}" 2>/dev/null || {
            warn "No se pudieron instalar algunos paquetes. Continuando con lo disponible."
        }
    fi
}

# ---------- MAIN ----------
trap 'echo -e "\n${ROJO}Ejecución interrumpida por el usuario${NC}"; exit 1' INT TERM

# Verificar root
if [ "$(id -u)" -ne 0 ]; then
    echo -e "${ROJO}[ERROR] Ejecutar con sudo: sudo $0${NC}"
    exit 1
fi

# Crear directorios
mkdir -p "$RECUPERADOS" "$LOGS" 2>/dev/null

# Instalar dependencias necesarias (foremost, testdisk, pv, smartctl)
install_deps

# Bucle principal del menú
while true; do
    mostrar_estado_particion
    mostrar_menu
    read -r opcion
    case $opcion in
        1)
            modo_reporte
            ;;
        2)
            recuperacion_completa
            ;;
        3)
            ver_log
            ;;
        4)
            evento "Saliendo del script."
            echo -e "${VERDE}¡Hasta luego!${NC}"
            exit 0
            ;;
        *)
            echo -e "${ROJO}Opción inválida. Intente de nuevo.${NC}"
            sleep 1
            ;;
    esac
done
