#!/usr/bin/env bash
# Baut HeadUnit unter Linux (CMake + Ninja + Systempakete), testet und startet es.
# Aufruf: bash BuildAndRun.sh [Optionen] [-- Argumente fuer HeadUnit]   (Hilfe: --help)
set -Eeuo pipefail

REPO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$REPO_DIR/HeadUnit"

# Debian, Ubuntu, Raspberry Pi OS: dieselbe Liste wie in HeadUnit/docs/linux.md und .github/workflows/build.yml.
BUILD_PACKAGES=(build-essential cmake ninja-build pkg-config git
  qt6-base-dev libboost-dev libssl-dev libprotobuf-dev protobuf-compiler
  libusb-1.0-0-dev libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev)
# Nur zum Ausfuehren noetig (Qt-Plattform-Plugin fuer X11).
RUNTIME_PACKAGES=(libxcb-cursor0)

BUILD_TYPE="debug"
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
  bash BuildAndRun.sh [Optionen] [debug|release] [-- Argumente fuer HeadUnit]

Ohne Optionen: Debug bauen, Tests laufen lassen, HeadUnit starten.

Optionen:
  debug | release     Build-Typ (Standard: debug)
  --pull              vorher "git pull --ff-only" ausfuehren
  --reset             vorher "git reset --hard" (verwirft lokale Aenderungen!), impliziert --pull
  --clean             Build-Ordner dieses Build-Typs vor dem Bauen loeschen
  --install-deps      fehlende Pakete per "sudo apt-get install" nachinstallieren (Debian/Ubuntu/Raspberry Pi OS)
  --udev              udev-Regel fuer den Handy-Zugriff installieren (einmalig, fragt nach sudo)
  --core-only         nur Kern ohne Qt und Bibliotheken bauen und testen (kein Programm, kein Start)
  --no-test           ctest ueberspringen
  --no-run            nur bauen (und testen), nicht starten
  -j, --jobs N        Anzahl paralleler Compiler-Prozesse (auf kleinen Raspberry Pi: -j 2)
  -h, --help          diese Hilfe

Alles nach "--" geht unveraendert an HeadUnit, zum Beispiel:
  bash BuildAndRun.sh release -- --display 1280x720
  bash BuildAndRun.sh --no-run
  bash BuildAndRun.sh --no-test -- --scan
  bash BuildAndRun.sh --install-deps --udev release
EOL
}

info()  { printf '\033[34m%s\033[0m\n' "$*"; }
ok()    { printf '\033[32m%s\033[0m\n' "$*"; }
warn()  { printf '\033[33m%s\033[0m\n' "$*" >&2; }
fail()  { printf '\033[31m%s\033[0m\n' "$*" >&2; exit 1; }

while [ "$#" -gt 0 ]; do
  case "$1" in
    debug|Debug|DEBUG)       BUILD_TYPE="debug" ;;
    release|Release|RELEASE) BUILD_TYPE="release" ;;
    --pull)                  DO_PULL="true" ;;
    --reset)                 DO_RESET="true"; DO_PULL="true" ;;
    --clean)                 DO_CLEAN="true" ;;
    --install-deps)          INSTALL_DEPS="true" ;;
    --udev)                  INSTALL_UDEV="true" ;;
    --core-only)             CORE_ONLY="true" ;;
    --no-test)               DO_TEST="false" ;;
    --no-run)                DO_RUN="false" ;;
    -j|--jobs)
      [ "$#" -ge 2 ] || fail "$1 braucht eine Zahl."
      JOBS="$2"; shift ;;
    -j[0-9]*)                JOBS="${1#-j}" ;;
    -h|--help)               print_usage; exit 0 ;;
    --)                      shift; HEADUNIT_ARGS=("$@"); break ;;
    *)
      printf '\033[31mUnbekannte Option: %s\033[0m\n\n' "$1" >&2
      print_usage >&2
      exit 1 ;;
  esac
  shift
done

if [ -n "$JOBS" ] && ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
  fail "Ungueltige Anzahl fuer --jobs: $JOBS"
fi

[ "$(uname -s)" = "Linux" ] || fail "Dieses Skript ist nur fuer Linux (gefunden: $(uname -s))."
[ -f "$PROJECT_DIR/CMakePresets.json" ] || fail "HeadUnit/CMakePresets.json nicht gefunden: BuildAndRun.sh muss im Wurzelordner des Repositorys liegen."

if [ "$CORE_ONLY" = "true" ]; then
  PRESET="linux-core-only"
  DO_RUN="false"
else
  PRESET="linux-$BUILD_TYPE"
fi
BUILD_DIR="$PROJECT_DIR/out/build/$PRESET"

# ---------------------------------------------------------------- Git

if [ "$DO_PULL" = "true" ]; then
  cd "$REPO_DIR"
  if [ "$DO_RESET" = "true" ]; then
    info "Lokale Aenderungen verwerfen (git reset --hard)..."
    git reset --hard
  fi
  info "Neueste Aenderungen holen (git pull --ff-only)..."
  git pull --ff-only || fail "git pull fehlgeschlagen."
  ok "Git ist aktuell."
fi

# ---------------------------------------------------------------- Pakete

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO="sudo"
fi

if command -v dpkg >/dev/null 2>&1 && command -v apt-get >/dev/null 2>&1; then
  wanted=("${BUILD_PACKAGES[@]}")
  [ "$CORE_ONLY" = "true" ] && wanted=(build-essential cmake ninja-build pkg-config git)
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
  info "udev-Regel installieren..."
  bash "$PROJECT_DIR/scripts/install-udev-rules.sh"
fi

# ---------------------------------------------------------------- Bauen

cd "$PROJECT_DIR"

if [ "$DO_CLEAN" = "true" ] && [ -d "$BUILD_DIR" ]; then
  info "Build-Ordner loeschen: $BUILD_DIR"
  rm -rf "$BUILD_DIR"
fi

info "Konfiguriere ($PRESET)..."
cmake --preset "$PRESET" || fail "CMake-Konfiguration fehlgeschlagen (fehlt ein Paket? Meldung oben lesen)."

info "Baue ($PRESET)..."
build_args=(--build --preset "$PRESET")
[ -n "$JOBS" ] && build_args+=(--parallel "$JOBS")
BUILD_LOG="$BUILD_DIR/build.log"
if ! cmake "${build_args[@]}" 2>&1 | tee "$BUILD_LOG"; then
  # Ninja baut parallel: die letzte Zeile im Terminal ist meist nicht die Fehlerursache.
  printf '\n\033[31m===== Erster Fehler =====\033[0m\n' >&2
  grep -m1 -A40 -E '^FAILED:' "$BUILD_LOG" >&2 || tail -n 40 "$BUILD_LOG" >&2
  if grep -qiE 'Killed|internal compiler error|cannot allocate memory|out of memory' "$BUILD_LOG"; then
    warn "Sieht nach Speichermangel aus: mit weniger parallelen Prozessen neu versuchen, z.B.  bash BuildAndRun.sh -j 1"
  fi
  fail "Build fehlgeschlagen. Vollstaendiges Log: $BUILD_LOG"
fi
ok "Build erfolgreich: $BUILD_DIR"

# ---------------------------------------------------------------- Tests

if [ "$DO_TEST" = "true" ]; then
  info "Tests ($PRESET)..."
  ctest --preset "$PRESET" || fail "Tests fehlgeschlagen."
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
# Im Build-Ordner starten: dort landet headunit.log.
cd "$BUILD_DIR"
set +e
./HeadUnit "${HEADUNIT_ARGS[@]}"
run_result=$?
set -e

if [ "$run_result" -ne 0 ]; then
  warn "HeadUnit beendet mit Exit-Code $run_result (Log: $BUILD_DIR/headunit.log)."
fi
exit "$run_result"
