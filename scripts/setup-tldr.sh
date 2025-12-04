#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PAGE_SRC="$REPO_DIR/docs/tldr-glitch.md"
QUIET=0

usage() {
  echo "Usage: $0 [--page PATH] [--quiet]"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --page)
      PAGE_SRC="$2"
      shift 2
      ;;
    --quiet|-q)
      QUIET=1
      shift
      ;;
    *)
      usage
      exit 1
      ;;
  esac
done

if [ ! -f "$PAGE_SRC" ]; then
  echo "TLDR source page not found at $PAGE_SRC" >&2
  exit 1
fi

CONFIG_COMMON="$HOME/.config/tldr/pages/common"
CONFIG_PAGE="$CONFIG_COMMON/glitch.md"

mkdir -p "$CONFIG_COMMON"
cp "$PAGE_SRC" "$CONFIG_PAGE"

# Python tldr client cache (symlink to persist across updates)
for platform in common linux; do
  cache_dir="$HOME/.cache/tldr/pages/$platform"
  mkdir -p "$cache_dir"
  ln -sf "$CONFIG_PAGE" "$cache_dir/glitch.md"
done

# tealdeer custom pages and cache
for platform in common linux; do
  custom_dir="$HOME/.local/share/tealdeer/pages/$platform"
  mkdir -p "$custom_dir"
  ln -sf "$CONFIG_PAGE" "$custom_dir/glitch.md"

  cache_dir="$HOME/.cache/tealdeer/tldr-pages/pages.en/$platform"
  mkdir -p "$cache_dir"
  cp "$PAGE_SRC" "$cache_dir/glitch.md"
done

if [ "$QUIET" -eq 0 ]; then
  echo "[+] Installed TLDR page from $PAGE_SRC"
  echo "    -> ~/.config/tldr/pages/common/glitch.md"
  echo "    -> ~/.cache/tldr/pages/{common,linux}/glitch.md (symlinked)"
  echo "    -> ~/.local/share/tealdeer/pages/{common,linux}/glitch.md"
fi
