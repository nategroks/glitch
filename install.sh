#!/usr/bin/env bash
set -e

CONFIG_DIR="$HOME/.config/glitch"
SHAPE_CFG="$CONFIG_DIR/shape.config"
COLOR_CFG="$CONFIG_DIR/color.config"
VARIANT_DIR="$CONFIG_DIR/variants"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

write_default_color_config() {
  local palette="${1:-${GLITCH_PALETTE:-${PALETTE:-miami}}}"
  local palette_label
  local bg1 bg2 bg3 bg4 fg_dis fg_ker fg_upt fg_mem fg_pipe
  local presets=(miami sunset neon)

  if [ "$palette" = "random" ]; then
    palette=${presets[$((RANDOM % ${#presets[@]}))]}
  fi

  case "$palette" in
    miami)
      palette="miami"
      palette_label="miami-neon"
      bg1="#0a0f2d"
      bg2="#251b4b"
      bg3="#7d2ff7"
      bg4="#f78fb3"
      fg_dis="#8ce9ff"
      fg_ker="#ff7edb"
      fg_upt="#ffd8ff"
      fg_mem="#ffd6a5"
      fg_pipe="#b3f4ff"
      ;;
    sunset)
      palette="sunset"
      palette_label="sunset-grid"
      bg1="#150915"
      bg2="#331437"
      bg3="#ff3fa4"
      bg4="#ffc387"
      fg_dis="#ffe6ff"
      fg_ker="#ff94c2"
      fg_upt="#ffd7b4"
      fg_mem="#f2c7ff"
      fg_pipe="#fff2d9"
      ;;
    neon|coast|aqua)
      palette="neon"
      palette_label="neon-coast"
      bg1="#0a1020"
      bg2="#0e2848"
      bg3="#1e6f9f"
      bg4="#b9f3ff"
      fg_dis="#d7e9ff"
      fg_ker="#ff9dee"
      fg_upt="#8ee3ff"
      fg_mem="#ffe3ff"
      fg_pipe="#c8f7ff"
      ;;
    *)
      palette="miami"
      palette_label="miami-neon"
      bg1="#0a0f2d"
      bg2="#251b4b"
      bg3="#7d2ff7"
      bg4="#f78fb3"
      fg_dis="#8ce9ff"
      fg_ker="#ff7edb"
      fg_upt="#ffd8ff"
      fg_mem="#ffd6a5"
      fg_pipe="#b3f4ff"
      ;;
  esac

  cat > "$COLOR_CFG" <<EOF
# ~/.config/glitch/color.config
# Baked-in palette: $palette_label (GLITCH_PALETTE=miami|sunset|neon|random)

BG1=$bg1
BG2=$bg2
BG3=$bg3
BG4=$bg4

FG_DIS=$fg_dis
FG_KER=$fg_ker
FG_UPT=$fg_upt
FG_MEM=$fg_mem
FG_PIPE=$fg_pipe
EOF
  echo "[+] Wrote $COLOR_CFG using palette: $palette_label"
}

collect_image_paths() {
  local paths=()

  add_path() {
    local p="$1"
    if [ -n "$p" ] && [ -f "$p" ]; then
      paths+=("$p")
    fi
  }

  # Only pull imagery from variants; prioritize the requested variant/noise.
  add_path "$VARIANT_DIR/$GLITCH_NOISE.png"
  add_path "$VARIANT_DIR/$GLITCH_VARIANT.png"

  shopt -s nullglob
  for f in "$VARIANT_DIR"/*.png; do
    add_path "$f"
  done
  shopt -u nullglob

  # Deduplicate while preserving order.
  declare -A seen=()
  for p in "${paths[@]}"; do
    if [ -z "${seen[$p]}" ]; then
      echo "$p"
      seen["$p"]=1
    fi
  done
}

write_palette_from_image() {
  local image_paths=("$@")
  [ ${#image_paths[@]} -eq 0 ] && return 1
  if ! command -v python3 >/dev/null 2>&1; then
    return 1
  fi

  local palette
  palette=$(python3 - "${image_paths[@]}" <<'PY'
import sys
from collections import Counter
from PIL import Image

def lum(c):
    r, g, b = [x / 255.0 for x in c]
    def f(x): return x/12.92 if x <= 0.03928 else ((x+0.055)/1.055)**2.4
    return 0.2126*f(r) + 0.7152*f(g) + 0.0722*f(b)

def contrast(c1, c2):
    l1, l2 = sorted((lum(c1), lum(c2)), reverse=True)
    return (l1 + 0.05) / (l2 + 0.05)

def mix(base, anchor, factor):
    return tuple(round(base[i]*(1-factor) + anchor[i]*factor) for i in range(3))

def distinct(colors, min_dist=28):
    out = []
    for c in colors:
        if all(sum((c[i]-o[i])**2 for i in range(3)) > min_dist*min_dist for o in out):
            out.append(c)
        if len(out) == 4:
            break
    return out[:4]

paths = sys.argv[1:]
img = None
for path in paths:
    try:
        img = Image.open(path).convert("RGBA")
        primary = path
        break
    except Exception:
        continue

if img is None:
    sys.exit(1)

img = img.resize((96, 96))
pixels = [p[:3] for p in img.getdata() if p[3] > 10]

if not pixels:
    sys.exit(1)

counts = Counter(pixels)
ordered = [c for c, _ in counts.most_common()]
bg = distinct(ordered)
if len(bg) < 4:
    bg = (bg + ordered)[:4]
bg = bg[:4]
bg.sort(key=lum)

fg = []
for base in bg:
    anchor = (255, 255, 255) if lum(base) < 0.55 else (0, 0, 0)
    factor = 0.45 if anchor == (255, 255, 255) else 0.35
    tinted = mix(base, anchor, factor)
    target = 3.5
    while contrast(tinted, (0, 0, 0)) < target and factor < 0.8:
        factor += 0.05
        tinted = mix(base, anchor, factor)
    fg.append(tuple(map(int, tinted)))

# Use a slightly brighter version of the darkest line for the pipe.
pipe = mix(bg[0], (255, 255, 255), 0.3)
if contrast(bg[0], pipe) < 3.5:
    pipe = mix(bg[0], (255, 255, 255), 0.45)

def to_hex(c):
    return '#%02x%02x%02x' % c

bg_hex = [to_hex(c) for c in bg]
fg_hex = [to_hex(c) for c in fg]

print(f"BG1={bg_hex[0]}")
print(f"BG2={bg_hex[1]}")
print(f"BG3={bg_hex[2]}")
print(f"BG4={bg_hex[3]}")
print(f"FG_DIS={fg_hex[0]}")
print(f"FG_KER={fg_hex[1]}")
print(f"FG_UPT={fg_hex[2]}")
print(f"FG_MEM={fg_hex[3]}")
print(f"FG_PIPE={to_hex(tuple(map(int, pipe)))}")
print(f"PRIMARY={primary}")
PY
) || return 1

  if [ -z "$palette" ]; then
    return 1
  fi

  cat > "$COLOR_CFG" <<EOF
# ~/.config/glitch/color.config
# Auto-generated from ${image_paths[0]}
$(echo "$palette" | grep -v '^PRIMARY=')
EOF
}

# 1. Ensure config directory & default configs
mkdir -p "$CONFIG_DIR"
mkdir -p "$VARIANT_DIR"

if [ ! -f "$VARIANT_DIR/README.txt" ]; then
  cat > "$VARIANT_DIR/README.txt" <<'EOF'
Drop PNGs here named after noise modes to pair them automatically.

Example filenames (one per noise):
  default.png
  signal.png
  ritual.png
  gate.png
  ... and so on for any listed GLITCH_NOISE mode.

glitch will randomly pick one on each run (or honor GLITCH_VARIANT).
EOF
fi

if [ ! -f "$SHAPE_CFG" ]; then
  cat > "$SHAPE_CFG" <<'EOF'
# ~/.config/glitch/shape.config
# Pick one:
# fedora, debian, ubuntu, slackware, gentoo, arch, lfs, linuxmint, nix, void

SHAPE=gentoo
EOF
fi

# Create default glitch.config if missing
if [ ! -f "$CONFIG_DIR/glitch.config" ]; then
  cat > "$CONFIG_DIR/glitch.config" <<'EOF'
# ~/.config/glitch/glitch.config

# Network image fetching (0 = disable)
NET_IMAGES=1

# Sources: picsum|unsplash|reddit
FETCH_SOURCE=picsum
FETCH_COUNT=2
FETCH_MAX=50

# Optional: override variant directory (also works via GLITCH_VARIANT_DIR)
# LOCAL_IMAGES_DIR=$HOME/pictures/glitch

# Stats: up to 10, choose from
# distro,kernel,uptime,mem,host,user,shell,cpu,ip,disk,ports,entropy
STATS=distro,kernel,uptime,mem,ip,disk,ports,cpu,entropy
EOF
fi

# Seed a few default square/circle PNGs if variants is empty
if ! ls "$VARIANT_DIR"/*.png >/dev/null 2>&1; then
  if [ -x "$SCRIPT_DIR/glitch" ]; then
    "$SCRIPT_DIR/glitch" --fetch-only >/dev/null 2>&1 || true
  fi
fi

palette_hint="${GLITCH_PALETTE:-${PALETTE:-}}"
if [ -n "$palette_hint" ] && [ "$palette_hint" != "auto" ]; then
  echo "[*] Using baked-in palette '$palette_hint' (set GLITCH_PALETTE=auto to sample images)."
  write_default_color_config "$palette_hint"
else
  mapfile -t IMAGE_SOURCES < <(collect_image_paths)
  if [ ${#IMAGE_SOURCES[@]} -gt 0 ]; then
    # If no variant/noise is locked, rotate the list so we pick a random primary image each run.
    if [ -z "$GLITCH_NOISE" ] && [ -z "$GLITCH_VARIANT" ] && [ ${#IMAGE_SOURCES[@]} -gt 1 ]; then
      idx=$((RANDOM % ${#IMAGE_SOURCES[@]}))
      primary=${IMAGE_SOURCES[$idx]}
      reordered=("$primary")
      for i in "${!IMAGE_SOURCES[@]}"; do
        if [ "$i" -ne "$idx" ]; then
          reordered+=("${IMAGE_SOURCES[$i]}")
        fi
      done
      IMAGE_SOURCES=("${reordered[@]}")
    fi

    if write_palette_from_image "${IMAGE_SOURCES[@]}"; then
      echo "[+] Generated $COLOR_CFG from ${#IMAGE_SOURCES[@]} image(s); primary: ${IMAGE_SOURCES[0]}"
    else
      echo "[!] Could not sample images from $VARIANT_DIR; falling back to baked-in palette."
      write_default_color_config
    fi
  else
    echo "[!] No images in $VARIANT_DIR; falling back to baked-in palette."
    write_default_color_config
  fi
fi

# 2. Load config values
# shellcheck source=/dev/null
. "$SHAPE_CFG"
# shellcheck source=/dev/null
. "$COLOR_CFG"

: "${SHAPE:?SHAPE not set in $SHAPE_CFG}"

# Foreground fallbacks mirror the BG palette if not explicitly set.
FG_DIS="${FG_DIS:-$BG1}"
FG_KER="${FG_KER:-$BG2}"
FG_UPT="${FG_UPT:-$BG3}"
FG_MEM="${FG_MEM:-$BG4}"
FG_PIPE="${FG_PIPE:-#ffffff}"

hex_to_rgb() {
  local hex="${1#\#}"
  if [ ${#hex} -ne 6 ]; then
    echo "255;255;255"
    return
  fi
  local r g b
  r=$((16#${hex:0:2}))
  g=$((16#${hex:2:2}))
  b=$((16#${hex:4:2}))
  echo "$r;$g;$b"
}

BG1_RGB=$(hex_to_rgb "$BG1")
BG2_RGB=$(hex_to_rgb "$BG2")
BG3_RGB=$(hex_to_rgb "$BG3")
BG4_RGB=$(hex_to_rgb "$BG4")

FG_PIPE_RGB=$(hex_to_rgb "$FG_PIPE")

FG_DIS_RGB=$(hex_to_rgb "$FG_DIS")
FG_KER_RGB=$(hex_to_rgb "$FG_KER")
FG_UPT_RGB=$(hex_to_rgb "$FG_UPT")
FG_MEM_RGB=$(hex_to_rgb "$FG_MEM")

# 3. Generate colors.h
cat > colors.h <<EOF
/* colors.h - generated by install.sh */
#ifndef GLITCH_COLORS_H
#define GLITCH_COLORS_H

#define ESC       "\\e"
#define CSI       ESC "["

#define F_RESET   CSI "0m"

#define BG1       CSI "48;2;${BG1_RGB}m"
#define BG2       CSI "48;2;${BG2_RGB}m"
#define BG3       CSI "48;2;${BG3_RGB}m"
#define BG4       CSI "48;2;${BG4_RGB}m"

#define FG_DIS    CSI "38;2;${FG_DIS_RGB}m"
#define FG_KER    CSI "38;2;${FG_KER_RGB}m"
#define FG_UPT    CSI "38;2;${FG_UPT_RGB}m"
#define FG_MEM    CSI "38;2;${FG_MEM_RGB}m"
#define FG_PIPE   CSI "38;2;${FG_PIPE_RGB}m"

#endif
EOF

# 4. Generate shape.h based on SHAPE
case "$SHAPE" in

  gentoo)
    cat > shape.h <<'EOF'
#ifndef GLITCH_SHAPE_H
#define GLITCH_SHAPE_H

/*  WIDE GENTOO SWIRL (4 lines tall, fixed-width rows) */

/* Base shape (all rows are 39 chars wide) */

#define SHAPE1    "        ___------_______               "
#define SHAPE2    "    ____/   .-.    .-.   \\____         "
#define SHAPE3    "  _/  _  \\  (   )  (   )  /  _  \\_     "
#define SHAPE4    "  \\______/\\__\\_/____\\_/__/\\______/     "

/*
 * For stable colors:
 *  - We only glitch rows 1, 3, 4 (SHAPE1/3/4_S*)
 *  - Row 2 (SHAPE2_S*) is forced to match SHAPE2 exactly,
 *    so the 'ker |' column never moves horizontally.
 */

/* Glitch phases for row 1 */

#define SHAPE1_S1 "        _--~~~---______               "
#define SHAPE1_S2 "        ___------______               "
#define SHAPE1_S3 "        ___------_______              "

/* Row 2: keep ALL glitch variants identical to SHAPE2 for alignment */

#define SHAPE2_S1 SHAPE2
#define SHAPE2_S2 SHAPE2
#define SHAPE2_S3 SHAPE2

/* Glitch phases for row 3 */

#define SHAPE3_S1 "  _/  _  \\  (  _)(   )  /  _  \\_     "
#define SHAPE3_S2 "  _/  _  \\  (_  ) ( _ )  /  _  \\_    "
#define SHAPE3_S3 "  _/  _  \\  (   )  (   )  /  _  \\_   "

/* Glitch phases for row 4 */

#define SHAPE4_S1 "  \\______/\\__\\_/ _--\\_/__/\\______/    "
#define SHAPE4_S2 "  \\______/\\__\\_/-___\\_/__/\\______/    "
#define SHAPE4_S3 "  \\______/\\__\\_/____\\_/__/\\______/    "

#endif
EOF
    ;;


  arch)
    cat > shape.h <<'EOF'
#ifndef GLITCH_SHAPE_H
#define GLITCH_SHAPE_H

#define SHAPE1     "   /\\  "
#define SHAPE2     "  /  \\ "
#define SHAPE3     " /_/\\_\\"
#define SHAPE4     "   ||  "

#define SHAPE1_R   "    /\\ "
#define SHAPE2_R   "   /  \\"
#define SHAPE3_R   "  /_/\\_"
#define SHAPE4_R   "    || "

#define SHAPE1_L   "  /\\   "
#define SHAPE2_L   " /  \\  "
#define SHAPE3_L   "/_/\\_  "
#define SHAPE4_L   " ||    "

#define SHAPE1_S1  "   /\\  "
#define SHAPE2_S1  "  /_ \\ "
#define SHAPE3_S1  " /_/\\_ "
#define SHAPE4_S1  "   ||  "

#define SHAPE1_S2  "   /\\  "
#define SHAPE2_S2  "  / /  "
#define SHAPE3_S2  " /_/\\  "
#define SHAPE4_S2  "   ||  "

#define SHAPE1_S3  "   /\\  "
#define SHAPE2_S3  "  /  \\ "
#define SHAPE3_S3  " /_/\\_\\"
#define SHAPE4_S3  "   ||  "

#endif
EOF
    ;;
  # TODO: add fedora, debian, ubuntu, slackware, lfs, linuxmint, nix, void
  *)
    echo "Warning: unknown SHAPE='$SHAPE', falling back to gentoo."
    sed -i 's/^SHAPE=.*/SHAPE=gentoo/' "$SHAPE_CFG"
    exec "$0"
    ;;
esac

echo "[+] Generated colors.h and shape.h for SHAPE=$SHAPE"

# 5. Build glitch
echo "[*] Building glitch..."
make glitch
echo "[+] Build complete."

# 6. Install man page to user-local manpath
MAN_SRC="glitch.1"
USER_MAN_DIR="$HOME/.local/share/man/man1"

if [ -f "$MAN_SRC" ]; then
  mkdir -p "$USER_MAN_DIR"
  install -m 644 "$MAN_SRC" "$USER_MAN_DIR/glitch.1"
  echo "[+] Installed man page to $USER_MAN_DIR"

  # Ensure ~/.local/share/man is in MANPATH via ~/.bashrc
  if ! manpath 2>/dev/null | grep -q "$HOME/.local/share/man"; then
    echo 'export MANPATH="$HOME/.local/share/man:$MANPATH"' >> "$HOME/.bashrc"
    echo "[+] Added user manpath to ~/.bashrc"
    echo "    Run: source ~/.bashrc"
  fi
else
  echo "[!] glitch.1 not found — skipping man install."
fi

# 7. Install TLDR/tealdeer page if available
TLDR_PAGE="tldr-glitch.md"

cat > "$TLDR_PAGE" <<'EOF'
# glitch

> Animated vaporwave system information.
> Configurable shapes and colors via `~/.config/glitch/`.
> More information: `man glitch`.

- Run with default animation:

`glitch`

- Run once (single frame, then exit):

`glitch --once`

- Set animation speed in milliseconds:

`glitch --speed 80`

- Use environment variable for speed:

`GLITCH_SPEED=200 glitch`

- Rebuild with new shape or colors after editing config:

`./install.sh`
EOF

if command -v tldr >/dev/null 2>&1 || command -v tealdeer >/dev/null 2>&1; then
  TLDR_DIR="$HOME/.config/tldr/pages/common"
  mkdir -p "$TLDR_DIR"
  cp "$TLDR_PAGE" "$TLDR_DIR/glitch.md"
  echo "[+] Installed TLDR page to: $TLDR_DIR/glitch.md"
else
  echo "[*] tldr/tealdeer not detected; kept TLDR page at: $TLDR_PAGE"
fi

echo
echo "[✓] Done. You can now run:"
echo "    ./glitch"
echo
echo "For 'man glitch', open a new shell or run:"
echo "    source ~/.bashrc"
echo
echo "To add glitch to PATH, run:"
echo "    ./path.sh && source ~/.bashrc"
