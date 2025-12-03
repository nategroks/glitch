#!/usr/bin/env bash

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "export PATH=\"$REPO_DIR:\$PATH\"" >> "$HOME/.bashrc"
echo "[+] Added glitch to your PATH in ~/.bashrc"
echo "Run: source ~/.bashrc"
