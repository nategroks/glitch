#!/usr/bin/env python3
"""
Minimal fetcher to populate ~/.config/glitch/variants with PNGs.

Sources:
  - reddit: top posts from wallpaper-ish subreddits (no auth)
  - picsum: random placeholder photos (no auth)
  - unsplash: random photos if UNSPLASH_ACCESS_KEY is set

Usage examples:
  python3 fetch_variants.py --source reddit --count 8
  python3 fetch_variants.py --source picsum --count 5
  python3 fetch_variants.py --source unsplash --count 6
"""

import argparse
import json
import os
import random
import re
import string
import sys
import time
import urllib.request
from pathlib import Path
from typing import Iterable, List, Optional, Tuple

VARIANTS_DIR = Path.home() / ".config" / "glitch" / "variants"


def log(msg: str) -> None:
    sys.stdout.write(f"[+] {msg}\n")


def warn(msg: str) -> None:
    sys.stderr.write(f"[!] {msg}\n")


def sanitize_name(title: str, fallback: str) -> str:
    title = title.lower()
    title = re.sub(r"[^a-z0-9]+", "-", title).strip("-")
    if not title:
        title = fallback
    return title[:40]


def download(url: str, dest: Path, timeout: int = 15) -> bool:
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "glitch-variant-fetch/1.0"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = resp.read()
    except Exception as exc:  # noqa: BLE001
        warn(f"download failed {url}: {exc}")
        return False

    dest.parent.mkdir(parents=True, exist_ok=True)
    try:
        with open(dest, "wb") as f:
            f.write(data)
        return True
    except Exception as exc:  # noqa: BLE001
        warn(f"write failed {dest}: {exc}")
        return False


def pick_ext(url: str) -> str:
    for ext in (".png", ".jpg", ".jpeg", ".webp"):
        if url.lower().endswith(ext):
            return ext
    return ".png"


def convert_to_png(path: Path) -> Optional[Path]:
    try:
        from PIL import Image  # type: ignore
    except Exception:
        return None

    try:
        img = Image.open(path)
        rgba = img.convert("RGBA")
        rgba.verify()  # quick check
        img = Image.open(path).convert("RGBA")  # reopen after verify
    except Exception as exc:  # noqa: BLE001
        warn(f"convert read failed {path}: {exc}")
        return None

    png_path = path.with_suffix(".png")
    try:
        img.save(png_path, format="PNG")
        if png_path != path:
            try:
                path.unlink()
            except Exception:
                pass
        # Verify saved PNG
        try:
            Image.open(png_path).verify()
        except Exception as exc:  # noqa: BLE001
            warn(f"verify failed {png_path}: {exc}")
            try:
                png_path.unlink()
            except Exception:
                pass
            return None
        return png_path
    except Exception as exc:  # noqa: BLE001
        warn(f"convert save failed {png_path}: {exc}")
        return None

    return png_path


def apply_square_crop(path: Path) -> Optional[Path]:
    """Center-crop the image to a square and keep alpha."""
    try:
        from PIL import Image  # type: ignore
    except Exception:
        return None

    try:
        img = Image.open(path).convert("RGBA")
    except Exception as exc:  # noqa: BLE001
        warn(f"square crop read failed {path}: {exc}")
        return None

    w, h = img.size
    size = min(w, h)
    left = (w - size) // 2
    top = (h - size) // 2
    img = img.crop((left, top, left + size, top + size))
    try:
        img.save(path, format="PNG")
        return path
    except Exception as exc:  # noqa: BLE001
        warn(f"square crop save failed {path}: {exc}")
        return None


def apply_circle_mask(path: Path) -> Optional[Path]:
    """Apply a circular alpha mask to the image (centered)."""
    try:
        from PIL import Image, ImageDraw  # type: ignore
    except Exception:
        return None

    try:
        img = Image.open(path).convert("RGBA")
    except Exception as exc:  # noqa: BLE001
        warn(f"circle mask read failed {path}: {exc}")
        return None

    w, h = img.size
    size = min(w, h)
    mask = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(mask)
    draw.ellipse(
        [(w - size) // 2, (h - size) // 2, (w + size) // 2, (h + size) // 2],
        fill=255,
    )
    img.putalpha(mask)
    try:
        img.save(path, format="PNG")
        return path
    except Exception as exc:  # noqa: BLE001
        warn(f"circle mask save failed {path}: {exc}")
        return None


def is_safe(title: str, url: str) -> bool:
    banned = [
        "nsfw",
        "porn",
        "nude",
        "sex",
        "gore",
        "blood",
        "violent",
        "kill",
        "murder",
        "drug",
        "heroin",
        "coke",
        "meth",
        "lsd",
    ]
    text = f"{title} {url}".lower()
    return not any(word in text for word in banned)


def fetch_reddit(count: int, subs: Iterable[str]) -> List[Tuple[str, str]]:
    urls = []
    for sub in subs:
        url = f"https://old.reddit.com/r/{sub}/top.json?limit=80&t=week"
        req = urllib.request.Request(url, headers={"User-Agent": "glitch-variant-fetch/1.0"})
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                data = json.load(resp)
        except Exception as exc:  # noqa: BLE001
            warn(f"reddit fetch failed for r/{sub}: {exc}")
            continue

        for child in data.get("data", {}).get("children", []):
            post = child.get("data", {})
            link = post.get("url_overridden_by_dest") or post.get("url")
            title = post.get("title") or sub
            if not link or not isinstance(link, str):
                continue
            if not any(link.lower().endswith(ext) for ext in (".png", ".jpg", ".jpeg", ".webp")):
                continue
            if not is_safe(title, link):
                continue
            urls.append((title, link))
            if len(urls) >= count * 3:
                break
        if len(urls) >= count * 3:
            break

    random.shuffle(urls)
    return urls[:count]


def fetch_picsum(count: int) -> List[Tuple[str, str]]:
    urls = []
    for _ in range(count):
        seed = "".join(random.choices(string.ascii_lowercase + string.digits, k=8))
        urls.append((f"picsum-{seed}", f"https://picsum.photos/seed/{seed}/512/512"))
    return urls


def fetch_unsplash(count: int) -> List[Tuple[str, str]]:
    access_key = os.getenv("UNSPLASH_ACCESS_KEY")
    if not access_key:
        warn("UNSPLASH_ACCESS_KEY not set; skipping unsplash")
        return []

    url = f"https://api.unsplash.com/photos/random?count={count}"
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Client-ID {access_key}",
            "User-Agent": "glitch-variant-fetch/1.0",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            data = json.load(resp)
    except Exception as exc:  # noqa: BLE001
        warn(f"unsplash fetch failed: {exc}")
        return []

    urls = []
    for idx, item in enumerate(data if isinstance(data, list) else []):
        link = item.get("urls", {}).get("regular") or item.get("urls", {}).get("full")
        title = item.get("id") or f"unsplash-{idx}"
        if link:
            urls.append((title, link))
    return urls[:count]


def main() -> None:
    parser = argparse.ArgumentParser(description="Fetch PNGs for glitch variants.")
    parser.add_argument("--source", choices=["reddit", "picsum", "unsplash"], default="reddit")
    parser.add_argument("--count", type=int, default=2, help="How many images to fetch")
    parser.add_argument("--max-files", type=int, default=50, help="Cap variants directory at this many files")
    parser.add_argument("--mask", action="store_true", help="Apply centered square crop to images (default)")
    parser.add_argument("--circle-mask", action="store_true", help="Apply circular mask to images")
    parser.add_argument("--no-mask", action="store_true", help="Do not mask/crop images")
    parser.add_argument(
        "--subreddits",
        default="wallpapers,EarthPorn,ImaginaryLandscapes,CityPorn,wallpaper",
        help="Comma-separated subreddit list for --source reddit",
    )
    args = parser.parse_args()

    random.seed(time.time())

    if args.source == "reddit":
        subs = [s.strip() for s in args.subreddits.split(",") if s.strip()]
        pairs = fetch_reddit(args.count, subs)
    elif args.source == "picsum":
        pairs = fetch_picsum(args.count)
    else:
        pairs = fetch_unsplash(args.count)

    if not pairs:
        warn("no images fetched; nothing to do")
        sys.exit(1)

    VARIANTS_DIR.mkdir(parents=True, exist_ok=True)
    saved = 0
    for idx, (title, url) in enumerate(pairs, start=1):
        name = sanitize_name(title, f"img-{idx}")
        ext = pick_ext(url)
        dest = VARIANTS_DIR / f"{name}{ext}"
        if download(url, dest):
            png_path = convert_to_png(dest) or dest
            # Apply square crop by default; circle only when requested; or skip with --no-mask.
            if args.circle_mask:
                png_path = apply_circle_mask(png_path) or png_path
            elif not args.no_mask:
                png_path = apply_square_crop(png_path) or png_path
            if png_path.exists():
                log(f"saved {png_path}")
                saved += 1
                if saved >= args.count:
                    break

    # Prune to max files
    if args.max_files > 0:
        files = sorted(VARIANTS_DIR.glob("*.png"), key=lambda p: p.stat().st_mtime)
        if len(files) > args.max_files:
            for p in files[: len(files) - args.max_files]:
                try:
                    p.unlink()
                    warn(f"pruned {p}")
                except Exception as exc:  # noqa: BLE001
                    warn(f"prune failed {p}: {exc}")

    log(f"done. saved {saved} file(s) to {VARIANTS_DIR}")


if __name__ == "__main__":
    main()
