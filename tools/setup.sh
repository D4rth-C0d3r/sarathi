#!/usr/bin/env bash
# sārathi — first-time machine + repo setup (macOS / Linux / WSL)
#
# Usage:
#   chmod +x setup.sh
#   ./setup.sh [target-dir]        # default: ./sarathi
#
# Put CLAUDE.md next to this script and it will be copied into the repo.
# Safe to re-run: every step checks before acting.
#
# What this touches (work-laptop friendly):
#   - ~/.platformio/        PlatformIO in its own venv (penv) + toolchains. Delete to undo.
#   - <repo>/.venv/         project venv for repo Python tools (pre-commit, later bleak).
#   - <repo>/               the repo itself.
#   Never: system Python, pip --user, shell rc files, global git config.
#   Asks first: VS Code extensions, ~/.local/bin symlinks, and (Linux) sudo for USB access.

set -euo pipefail

REPO_DIR="${1:-sarathi}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORIG_PATH="$PATH"

PIOARDUINO_URL="https://github.com/pioarduino/platform-espressif32/releases/download/55.03.38/platform-espressif32.zip"  # Arduino 3.3.8 / IDF 5.5.4
PIO_INSTALLER_URL="https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py"
ELECROW_REPO="https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen.git"
PIO_UDEV_URL="https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules"
PIO_PENV="$HOME/.platformio/penv"

# ---------- helpers ----------
c_ok=$'\e[32m'; c_warn=$'\e[33m'; c_err=$'\e[31m'; c_hd=$'\e[1;36m'; c_off=$'\e[0m'
step() { echo; echo "${c_hd}==> $*${c_off}"; }
ok()   { echo "  ${c_ok}✓${c_off} $*"; }
warn() { echo "  ${c_warn}!${c_off} $*"; }
die()  { echo "  ${c_err}✗ $*${c_off}"; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }
ask()  { local a; read -r -p "  $1 [y/N] " a; [[ "$a" =~ ^[Yy]$ ]]; }
on_orig_path() { [[ ":$ORIG_PATH:" == *":$1:"* ]]; }

OS="$(uname -s)"
ME="${USER:-$(id -un)}"
IS_WSL=false; grep -qi microsoft /proc/version 2>/dev/null && IS_WSL=true
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
CHANGES=()   # summary of machine-level changes, printed at the end

# ---------- 1. prerequisites ----------
step "Checking prerequisites"

have git  || die "git not found. Install it (macOS: xcode-select --install; Debian/Ubuntu: sudo apt install git)."
ok "git $(git --version | awk '{print $3}')"
have curl || die "curl not found. Install it with your package manager."

PY=""
for p in python3.13 python3.12 python3.11 python3.10 python3; do
  if have "$p"; then
    v="$("$p" -c 'import sys;print(f"{sys.version_info[0]}.{sys.version_info[1]}")')"
    case "$v" in 3.10|3.11|3.12|3.13) PY="$p"; break;; esac
  fi
done
[[ -n "$PY" ]] || die "Python 3.10–3.13 required (pioarduino doesn't support other versions)."
"$PY" -c 'import venv, ensurepip' 2>/dev/null \
  || die "$PY can't create venvs. Debian/Ubuntu: sudo apt install python3-venv (match your Python version)."
ok "python $($PY --version | awk '{print $2}') ($PY), venv available"

if have g++ || have clang++; then
  ok "host C++ compiler found (needed for 'pio test -e native')"
else
  warn "no host C++ compiler. Native tests need one:"
  warn "  macOS: xcode-select --install   Debian/Ubuntu: sudo apt install build-essential"
fi

if have code; then ok "VS Code CLI ('code')"; else
  warn "'code' CLI not on PATH. In VS Code: Cmd/Ctrl+Shift+P → 'Shell Command: Install code in PATH'."
fi

if have java && java -version 2>&1 | grep -qE 'version "(17|21)'; then ok "JDK 17/21 (Android, Phase 4)"; else
  warn "JDK 17+ not found. Not needed until the Android app (install Android Studio then)."
fi

# ---------- 2. PlatformIO Core in its own venv ----------
step "PlatformIO Core (isolated venv: ~/.platformio/penv)"
export PATH="$PIO_PENV/bin:$PATH"
if [[ -x "$PIO_PENV/bin/pio" ]]; then
  ok "pio $("$PIO_PENV/bin/pio" --version | awk '{print $NF}') already in $PIO_PENV"
elif have pio; then
  ok "pio $(pio --version | awk '{print $NF}') already installed at $(command -v pio). Using it as is."
else
  echo "  Using PlatformIO's official installer: it creates its own venv; system Python is untouched."
  curl -fsSL "$PIO_INSTALLER_URL" -o "$TMP/get-platformio.py" \
    || die "couldn't download the installer. Behind a corporate proxy? Set HTTPS_PROXY and re-run."
  "$PY" "$TMP/get-platformio.py" || die "PlatformIO installer failed (see output above)."
  [[ -x "$PIO_PENV/bin/pio" ]] || die "installer finished but $PIO_PENV/bin/pio is missing."
  ok "pio $(pio --version | awk '{print $NF}') installed in $PIO_PENV"
  # shellcheck disable=SC2088  # literal ~ is for display
  CHANGES+=("~/.platformio/  PlatformIO venv + toolchains (rm -rf ~/.platformio to undo)")
fi

# Make 'pio' available in new terminals without editing shell rc files
if ! PATH="$ORIG_PATH" command -v pio >/dev/null 2>&1; then
  if on_orig_path "$HOME/.local/bin"; then
    if ask "Symlink pio/platformio into ~/.local/bin (already on your PATH)?"; then
      for b in pio platformio; do ln -sf "$PIO_PENV/bin/$b" "$HOME/.local/bin/$b"; done
      ok "symlinked"
      # shellcheck disable=SC2088
      CHANGES+=("~/.local/bin/pio, platformio  symlinks into the PlatformIO venv")
    fi
  fi
  if ! PATH="$ORIG_PATH" command -v pio >/dev/null 2>&1 && [[ ! -L "$HOME/.local/bin/pio" ]]; then
    warn "'pio' isn't on your PATH in new terminals. Add this line to ~/.zshrc or ~/.bashrc yourself:"
    warn "  export PATH=\"\$HOME/.platformio/penv/bin:\$PATH\""
    warn "(The VS Code PlatformIO extension finds it without this.)"
  fi
fi

step "Pre-downloading pioarduino platform (Arduino 3.3.8) into ~/.platformio. Large; takes a while."
if pio pkg install --global --platform "$PIOARDUINO_URL" >/dev/null 2>&1; then
  ok "platform cached"
else
  warn "pre-download failed. It will download on the first 'pio run' instead."
fi

# ---------- 3. VS Code extensions ----------
if have code; then
  step "VS Code extensions"
  for ext in platformio.platformio-ide anthropic.claude-code; do
    if code --list-extensions 2>/dev/null | grep -qi "^$ext$"; then ok "$ext"
    elif ask "Install VS Code extension $ext?"; then
      if code --install-extension "$ext" >/dev/null; then ok "$ext installed"; CHANGES+=("VS Code extension $ext")
      else warn "couldn't install $ext (your VS Code may restrict extensions)"; fi
    fi
  done
fi

# ---------- 4. USB access (Linux only, needs sudo, always asks) ----------
if [[ "$OS" == "Linux" && "$IS_WSL" == false ]]; then
  step "USB serial access (Linux)"
  if [[ ! -f /etc/udev/rules.d/99-platformio-udev.rules ]]; then
    if ask "Install PlatformIO udev rules (needs sudo)?"; then
      curl -fsSL "$PIO_UDEV_URL" | sudo tee /etc/udev/rules.d/99-platformio-udev.rules >/dev/null
      sudo udevadm control --reload-rules && sudo udevadm trigger
      ok "udev rules installed"; CHANGES+=("/etc/udev/rules.d/99-platformio-udev.rules")
    else warn "skipped. Flashing may need sudo or fail with 'permission denied'."; fi
  else ok "udev rules present"; fi
  grp="dialout"; getent group uucp >/dev/null && ! getent group dialout >/dev/null && grp="uucp"
  if id -nG "$ME" | grep -qw "$grp"; then ok "user in '$grp' group"; else
    if ask "Add $ME to '$grp' group (needs sudo, then log out/in)?"; then
      sudo usermod -aG "$grp" "$ME" && warn "added. Log out and back in before flashing."
      CHANGES+=("added $ME to group $grp")
    fi
  fi
fi
if $IS_WSL; then
  warn "WSL can't see USB devices by default. Flash from Windows (tools/setup.ps1) or use usbipd-win."
fi

# ---------- 5. repo skeleton ----------
step "Repository skeleton in ./$REPO_DIR"
mkdir -p "$REPO_DIR"; cd "$REPO_DIR"
if [[ ! -d .git ]]; then git init -q -b main; ok "git repo initialised (branch main)"; else ok "git repo exists"; fi

dirs=(
  .github/workflows .github/ISSUE_TEMPLATE
  docs/protocol/schema docs/protocol/vectors docs/adr
  firmware/src firmware/boards firmware/assets firmware/test
  firmware/lib/sarathi_core firmware/lib/sarathi_proto firmware/lib/sarathi_board
  firmware/lib/sarathi_link firmware/lib/sarathi_ota firmware/lib/sarathi_settings
  firmware/lib/sarathi_ui
  android hardware/enclosure hardware/wiring tools vendor
)
for d in "${dirs[@]}"; do
  mkdir -p "$d"
  [[ -z "$(ls -A "$d")" ]] && touch "$d/.gitkeep"
done
ok "directories created"

if [[ ! -f .gitignore ]]; then
cat > .gitignore <<'EOF'
# PlatformIO
.pio/
firmware/.pio/
.vscode/.browse.c_cpp.db*
.vscode/c_cpp_properties.json
.vscode/launch.json
.vscode/ipch
# Android / Gradle
android/.gradle/
android/build/
android/**/build/
android/local.properties
android/.idea/
*.apk
*.aab
*.keystore
*.jks
# Python
__pycache__/
.venv/
# Secrets
*.pem
*.key
secrets/
.env
# OS / editors
.DS_Store
Thumbs.db
*.swp
EOF
ok ".gitignore written"; fi

if [[ -f "$SCRIPT_DIR/CLAUDE.md" && ! -f CLAUDE.md ]]; then
  cp "$SCRIPT_DIR/CLAUDE.md" CLAUDE.md; ok "CLAUDE.md copied"
elif [[ -f CLAUDE.md ]]; then ok "CLAUDE.md present"
else warn "CLAUDE.md not found next to this script. Copy it into $REPO_DIR/ manually."; fi

# Keep the setup scripts in the repo for the next contributor
for s in setup.sh setup.ps1; do
  [[ -f "$SCRIPT_DIR/$s" && ! -f "tools/$s" ]] && cp "$SCRIPT_DIR/$s" "tools/$s"
done
rm -f tools/.gitkeep 2>/dev/null || true

# ---------- 6. project venv for repo Python tools ----------
step "Project venv (.venv) for repo Python tools"
if [[ ! -x .venv/bin/python ]]; then
  "$PY" -m venv .venv
  ok ".venv created with $PY"
else ok ".venv exists"; fi
.venv/bin/python -m pip install -q --upgrade pip
.venv/bin/python -m pip install -q pre-commit
ok "pre-commit installed in .venv (hooks get enabled once .pre-commit-config.yaml exists)"

# ---------- 7. Elecrow examples as a submodule ----------
step "Elecrow examples → vendor/crowpanel-2.1 (shallow submodule, ~420 MB)"
if [[ ! -e vendor/crowpanel-2.1/.git ]]; then
  rm -f vendor/.gitkeep
  git submodule add --depth 1 "$ELECROW_REPO" vendor/crowpanel-2.1
  git config -f .gitmodules submodule.vendor/crowpanel-2.1.shallow true
  ok "submodule added"
else ok "submodule present"; fi

# ---------- 8. first commit ----------
step "Initial commit"
if git rev-parse --verify HEAD >/dev/null 2>&1; then ok "repo already has commits"
elif [[ -z "$(git config user.email || true)" ]]; then
  warn "git user.email not set. For this repo only (keeps your global work identity untouched):"
  warn "  git config user.name 'You'; git config user.email you@example.com"
  warn "then: git add -A && git commit -m 'chore: initial repo skeleton'"
else
  warn "committing as: $(git config user.name) <$(git config user.email)>"
  warn "If that's your work identity and you want a personal one for this repo, answer N, then run"
  warn "  git config user.email <personal>; git add -A && git commit -m 'chore: initial repo skeleton'"
  if ask "Commit with this identity?"; then
    git add -A && git commit -q -m "chore: initial repo skeleton" && ok "committed"
  fi
fi

# ---------- 9. board check ----------
step "Board check"
echo "  Connect the CrowPanel's USB IN port with a DATA cable, then press Enter."
read -r _
pio device list 2>/dev/null | grep -E '^(/dev|COM)' || true
if pio device list 2>/dev/null | grep -qiE '303A|usbmodem|ttyACM'; then
  ok "ESP32-S3 native USB port detected"
else
  warn "no ESP32-S3 port seen. Try another cable/port, or hold BOOT while plugging in."
fi

# ---------- done ----------
step "Done"
echo
echo "  Changes outside the repo:"
if ((${#CHANGES[@]})); then for c in "${CHANGES[@]}"; do echo "    - $c"; done
else echo "    - none (everything was already in place)"; fi
cat <<EOF

  Repo: $(pwd)

  Next:
    1. Create an empty GitHub repo named 'sarathi', then:
         git remote add origin git@github.com:<you>/sarathi.git && git push -u origin main
    2. For repo Python tools:   source .venv/bin/activate
    3. Open the folder in VS Code:   code .
    4. Start Claude Code and paste:

       Read CLAUDE.md. We're in Phase 0. Plan (don't code yet) the repo scaffold:
       community files, configs (.clang-format, .clang-tidy, .editorconfig,
       .pre-commit-config.yaml), firmware/platformio.ini with crowpanel21 + native
       envs using the pinned versions, firmware/partitions.csv, an empty main.cpp
       that builds, an empty Android Compose app, CI workflows, ADR-0001, and
       docs/building.md. List files and the order you'll create them in.

EOF
