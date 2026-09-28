#!/usr/bin/env bash
# Baut HeadUnit unter Linux (CMake + Ninja + Systempakete), testet und startet es.
# Aufruf: bash BuildAndRun.sh [Build-Profil] [Build-Optionen] [-- Argumente fuer HeadUnit]   (Hilfe: --help)
set -Eeuo pipefail

REPO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$REPO_DIR/HeadUnit"
SCRIPT_FILE="$REPO_DIR/BuildAndRun.sh"
# Fuer den Neustart nach "--pull" (siehe unten).
ORIGINAL_ARGS=("$@")

# Debian, Ubuntu, Raspberry Pi OS: dieselbe Liste wie in HeadUnit/docs/linux.md und .github/workflows/build.yml.
BUILD_PACKAGES=(build-essential cmake ninja-build pkg-config git
  qt6-base-dev libboost-dev libssl-dev libprotobuf-dev protobuf-compiler
  libusb-1.0-0-dev libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev)
CORE_PACKAGES=(build-essential cmake ninja-build pkg-config git)
# Nur zum Ausfuehren noetig (Qt-Plattform-Plugin fuer X11).
RUNTIME_PACKAGES=(libxcb-cursor0)

PROFILE=""
DO_PULL="false"
DO_RESET="false"
DO_CLEAN="false"
DO_TEST="true"
DO_RUN="true"
CORE_ONLY="false"
INSTALL_DEPS="false"
INSTALL_UDEV="false"
JOBS=""
HEADUNIT_ARGS=()

print_usage() {
  cat <<'EOL'
Aufruf:
  bash BuildAndRun.sh [Build-Profil] [Build-Optionen] [-- Argumente fuer HeadUnit]

Ohne Argumente: Profil debug bauen, Tests ausfuehren, HeadUnit starten.

Build-Profile (hoechstens eines):
  debug               Entwicklung: Debug-Symbole, ohne Optimierung, ausfuehrliches Log (Standard)
  release             Optimiert, Log ohne Debug-Zeilen
  debug_level_log     wie debug, dazu das vollstaendige Log (auch das Android-Auto-Protokoll)
  release_level_log   wie release (gleich optimiert), dazu das vollstaendige Log
                      (debug-level-log und release-level-log werden auch angenommen)

Build-Optionen:
  --clean             Ausgabeordner des gewaehlten Profils vor dem Bauen loeschen
  --no-test           Tests (ctest) ueberspringen
  --no-run            nur bauen und testen, HeadUnit nicht starten
  --install-deps      fehlende Pakete per "sudo apt-get install" nachinstallieren (Debian/Ubuntu/Raspberry Pi OS)
  --pull              vorher "git pull --ff-only" ausfuehren; lokale Aenderungen bleiben erhalten
  --reset             DESTRUKTIV: verwirft vorher alle lokalen Aenderungen ("git reset --hard")
                      und fuehrt danach "git pull --ff-only" aus
  -j N, --jobs N      Anzahl paralleler Compiler-Prozesse (auf kleinen Raspberry Pi: -j 2)
  -h, --help          diese Hilfe

Projektspezifische Optionen:
  --core-only         nur den Kern ohne Qt und Bibliotheken bauen und testen (Profil debug, kein Start)
  --install-udev      udev-Regel fuer den Handy-Zugriff installieren (einmalig, fragt nach sudo; alt: --udev)

Anwendungsargumente:
  -- <Argumente>      alles nach "--" geht unveraendert an HeadUnit

Ausgabe: HeadUnit/bin/linux<Architektur>/<Profil>/ (Programm, Tests, build.log, headunit.log)

Beispiele:
  bash BuildAndRun.sh release -- --display 1280x720
  bash BuildAndRun.sh release --clean --no-run -j 2
  bash BuildAndRun.sh debug_level_log --no-test -- --scan
  bash BuildAndRun.sh --install-deps --install-udev release
EOL
}

info()  { printf '\033[34m%s\033[0m\n' "$*"; }
ok()    { printf '\033[32m%s\033[0m\n' "$*"; }
warn()  { printf '\033[33m%s\033[0m\n' "$*" >&2; }
fail()  { printf '\033[31m%s\033[0m\n' "$*" >&2; exit 1; }

set_profile() {
  [ -z "$PROFILE" ] || fail "Nur ein Build-Profil angeben (schon gewaehlt: $PROFILE, dazu: $1)."
  PROFILE="$1"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    debug)                              set_profile debug ;;
    release)                            set_profile release ;;
    debug_level_log|debug-level-log)    set_profile debug_level_log ;;
    release_level_log|release-level-log) set_profile release_level_log ;;
    --clean)                 DO_CLEAN="true" ;;
    --no-test)               DO_TEST="false" ;;
    --no-run)                DO_RUN="false" ;;
    --install-deps)          INSTALL_DEPS="true" ;;
    --pull)                  DO_PULL="true" ;;
    --reset)                 DO_RESET="true"; DO_PULL="true" ;;
    --core-only)             CORE_ONLY="true" ;;
    --install-udev|--udev)   INSTALL_UDEV="true" ;;
    -j|--jobs)
      [ "$#" -ge 2 ] || fail "$1 braucht eine Zahl."
      JOBS="$2"; shift ;;
    -j[0-9]*)                JOBS="${1#-j}" ;;
    -h|--help)               print_usage; exit 0 ;;
    --)                      shift; HEADUNIT_ARGS=("$@"); break ;;
    *)
      printf '\033[31mUnbekanntes Argument: %s\033[0m\n\n' "$1" >&2
      print_usage >&2
      exit 1 ;;
  esac
  shift
done

if [ -n "$JOBS" ] && ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
  fail "Ungueltige Anzahl fuer --jobs: $JOBS"
fi
[ -n "$PROFILE" ] || PROFILE="debug"
if [ "$CORE_ONLY" = "true" ]; then
  [ "$PROFILE" = "debug" ] || fail "--core-only gibt es nur mit dem Profil debug (gewaehlt: $PROFILE)."
  DO_RUN="false"
fi

[ "$(uname -s)" = "Linux" ] || fail "Dieses Skript ist nur fuer Linux (gefunden: $(uname -s))."
[ -f "$PROJECT_DIR/CMakePresets.json" ] || fail "HeadUnit/CMakePresets.json nicht gefunden: BuildAndRun.sh muss im Wurzelordner des Repositorys liegen."

# Die Architektur bestimmt das Preset und den Ausgabeordner (bin/linux<Architektur>/<Profil>/).
case "$(uname -m)" in
  x86_64|amd64)          ARCHITECTURE="x64";   PRESET_PREFIX="linux" ;;
  aarch64|arm64)         ARCHITECTURE="arm64"; PRESET_PREFIX="linux-arm64" ;;
  armv6*|armv7*|armhf)   ARCHITECTURE="arm";   PRESET_PREFIX="linux-arm" ;;
  *) fail "Nicht unterstuetzte Architektur: $(uname -m) (unterstuetzt: x86_64, aarch64, armv7)." ;;
esac
if [ "$CORE_ONLY" = "true" ]; then
  PRESET="$PRESET_PREFIX-core-only"
else
  PRESET="$PRESET_PREFIX-${PROFILE//_/-}"
fi
OUTPUT_DIR="$PROJECT_DIR/bin/linux$ARCHITECTURE/$PROFILE"
BUILD_LOG="$OUTPUT_DIR/build.log"

# ---------------------------------------------------------------- Git

if [ "$DO_PULL" = "true" ]; then
  cd "$REPO_DIR"
  script_before="$(cksum < "$SCRIPT_FILE")"
  if [ "$DO_RESET" = "true" ]; then
    warn "Verwerfe alle lokalen Aenderungen (git reset --hard) ..."
    git reset --hard
  fi
  info "Neueste Aenderungen holen (git pull --ff-only) ..."
  git pull --ff-only || fail "git pull fehlgeschlagen (lokale Aenderungen? Sie wurden nicht angetastet)."
  ok "Git ist aktuell."
  # Bash hat dieses Skript schon vor dem Pull gelesen: Hat der Pull (oder das Reset) das Skript selbst geaendert,
  # liefe sonst noch die alte Fassung weiter (z. B. mit einer Paketliste, der neu dazugekommene Pakete fehlen).
  # Darum startet es sich dann sofort in der neuen Fassung neu, mit denselben Argumenten, nur ohne --pull/--reset.
  if [ "$(cksum < "$SCRIPT_FILE")" != "$script_before" ]; then
    info "BuildAndRun.sh wurde aktualisiert: starte die neue Fassung ..."
    rerun=()
    is_passthrough="false"
    for arg in ${ORIGINAL_ARGS[@]+"${ORIGINAL_ARGS[@]}"}; do
      if [ "$is_passthrough" = "false" ]; then
        [ "$arg" = "--" ] && is_passthrough="true"
        [ "$arg" = "--pull" ] || [ "$arg" = "--reset" ] && continue
      fi
      rerun+=("$arg")
    done
    exec bash "$SCRIPT_FILE" ${rerun[@]+"${rerun[@]}"}
  fi
fi

# ---------------------------------------------------------------- Pakete

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO="sudo"
fi

if command -v dpkg >/dev/null 2>&1 && command -v apt-get >/dev/null 2>&1; then
  wanted=("${BUILD_PACKAGES[@]}")
  [ "$CORE_ONLY" = "true" ] && wanted=("${CORE_PACKAGES[@]}")
  [ "$DO_RUN" = "true" ] && wanted+=("${RUNTIME_PACKAGES[@]}")

  missing=()
  for package in "${wanted[@]}"; do
    dpkg -s "$package" >/dev/null 2>&1 || missing+=("$package")
  done

  if [ "${#missing[@]}" -gt 0 ]; then
    if [ "$INSTALL_DEPS" = "true" ]; then
      info "Installiere fehlende Pakete: ${missing[*]}"
      $SUDO apt-get update
      $SUDO apt-get install -y "${missing[@]}"
    else
      warn "Fehlende Pakete: ${missing[*]}"
      warn "Nachinstallieren mit:  sudo apt install ${missing[*]}"
      warn "oder dieses Skript mit --install-deps starten."
      exit 1
    fi
  fi
else
  warn "Kein apt gefunden: Paketpruefung uebersprungen (Paketliste: HeadUnit/docs/linux.md)."
  [ "$INSTALL_DEPS" = "true" ] && warn "--install-deps kennt nur apt."
fi

for tool in cmake ninja g++ pkg-config; do
  command -v "$tool" >/dev/null 2>&1 || fail "Fehlendes Programm: $tool"
done

# CMakeLists.txt verlangt CMake 3.24 oder neuer.
cmake_version="$(cmake --version | head -n1 | sed -E 's/[^0-9]*([0-9]+\.[0-9]+).*/\1/')"
if [ "$(printf '%s\n3.24\n' "$cmake_version" | sort -V | head -n1)" != "3.24" ]; then
  fail "CMake $cmake_version ist zu alt, HeadUnit braucht 3.24 oder neuer (Ubuntu 22.04: siehe HeadUnit/docs/linux-setup.md)."
fi

# ---------------------------------------------------------------- udev

if [ "$INSTALL_UDEV" = "true" ]; then
  info "udev-Regel installieren ..."
  bash "$PROJECT_DIR/scripts/install-udev-rules.sh"
fi

# ---------------------------------------------------------------- Bauen

cd "$PROJECT_DIR"

if [ "$DO_CLEAN" = "true" ] && [ -d "$OUTPUT_DIR" ]; then
  info "Ausgabeordner loeschen: $OUTPUT_DIR"
  rm -rf "$OUTPUT_DIR"
fi
mkdir -p "$OUTPUT_DIR"

info "Konfiguriere ($PRESET) ..."
cmake --preset "$PRESET" 2>&1 | tee "$BUILD_LOG" || fail "CMake-Konfiguration fehlgeschlagen (fehlt ein Paket? Meldung oben lesen, Log: $BUILD_LOG)."

info "Baue ($PRESET) ..."
build_args=(--build --preset "$PRESET")
[ -n "$JOBS" ] && build_args+=(--parallel "$JOBS")
if ! cmake "${build_args[@]}" 2>&1 | tee -a "$BUILD_LOG"; then
  # Ninja baut parallel: die letzte Zeile im Terminal ist meist nicht die Fehlerursache.
  printf '\n\033[31m===== Erster Fehler =====\033[0m\n' >&2
  grep -m1 -A40 -E '^FAILED:' "$BUILD_LOG" >&2 || tail -n 40 "$BUILD_LOG" >&2
  if grep -qiE 'Killed|internal compiler error|cannot allocate memory|out of memory' "$BUILD_LOG"; then
    warn "Sieht nach Speichermangel aus: mit weniger parallelen Prozessen neu versuchen, z. B.  bash BuildAndRun.sh -j 1"
  fi
  fail "Build fehlgeschlagen. Vollstaendiges Log: $BUILD_LOG"
fi
ok "Build erfolgreich: $OUTPUT_DIR"

# ---------------------------------------------------------------- Tests

if [ "$DO_TEST" = "true" ]; then
  info "Tests ($PRESET) ..."
  ctest --preset "$PRESET" || fail "Tests fehlgeschlagen; HeadUnit wird nicht gestartet."
  ok "Tests bestanden."
else
  warn "Tests uebersprungen."
fi

# ---------------------------------------------------------------- Starten

if [ "$DO_RUN" != "true" ]; then
  warn "Programmstart uebersprungen."
  exit 0
fi

# Ohne Bildschirm kann Qt kein Fenster oeffnen (typisch: SSH). --scan und die --test-* Laeufe brauchen nur teils eines.
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${QT_QPA_PLATFORM:-}" ]; then
  warn "Kein Bildschirm gefunden (DISPLAY/WAYLAND_DISPLAY leer)."
  warn "Am Rechner selbst starten, oder ohne Fenster testen:  QT_QPA_PLATFORM=offscreen bash BuildAndRun.sh -- --scan"
fi

ok "Starte HeadUnit ${HEADUNIT_ARGS[*]:-}"
# Im Ausgabeordner starten: dort landet headunit.log.
cd "$OUTPUT_DIR"
set +e
./HeadUnit ${HEADUNIT_ARGS[@]+"${HEADUNIT_ARGS[@]}"}
run_result=$?
set -e

if [ "$run_result" -ne 0 ]; then
  warn "HeadUnit beendet mit Exit-Code $run_result (Log: $OUTPUT_DIR/headunit.log)."
fi
exit "$run_result"
