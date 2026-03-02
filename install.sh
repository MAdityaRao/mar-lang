#!/bin/bash
set -e

VERSION="v1.5.1"
REPO="MAdityaRao/mar-lang"
BASE="https://github.com/$REPO/releases/download/$VERSION"

# ── Colors ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ── Detect OS & Arch ─────────────────────────────────────────────────────────
OS=$(uname -s)
ARCH=$(uname -m)

case "$OS" in
  Darwin)
    if [ "$ARCH" = "arm64" ]; then
      BINARY="mar-macos-arm64"
      PLATFORM="macOS Apple Silicon"
    else
      BINARY="mar-macos-x86_64"
      PLATFORM="macOS Intel"
    fi
    ;;
  Linux)
    BINARY="mar-linux-x86_64"
    PLATFORM="Linux x86_64"
    ;;
  MINGW*|MSYS*|CYGWIN*)
    echo -e "${RED}Windows detected.${NC}"
    echo "Please run the Windows installer instead:"
    echo ""
    echo "  PowerShell (as Admin):"
    echo "  irm https://github.com/$REPO/releases/download/$VERSION/install.ps1 | iex"
    exit 1
    ;;
  *)
    echo -e "${RED}Unsupported platform: $OS / $ARCH${NC}"
    echo "Download manually from: https://github.com/$REPO/releases/tag/$VERSION"
    exit 1
    ;;
esac

# ── Install ───────────────────────────────────────────────────────────────────
echo -e "${BOLD}Mar Language Installer${NC}"
echo -e "Version : ${CYAN}$VERSION${NC}"
echo -e "Platform: ${CYAN}$PLATFORM${NC}"
echo ""

if ! command -v curl &>/dev/null; then
  echo -e "${RED}Error: curl is required but not installed.${NC}"
  exit 1
fi

echo "Downloading..."
curl -fsSL --progress-bar "$BASE/$BINARY" -o /tmp/mar_install

chmod +x /tmp/mar_install

echo "Installing to /usr/local/bin/mar..."
if [ -w /usr/local/bin ]; then
  mv /tmp/mar_install /usr/local/bin/mar
else
  sudo mv /tmp/mar_install /usr/local/bin/mar
fi

# ── Verify ────────────────────────────────────────────────────────────────────
if command -v mar &>/dev/null; then
  echo ""
  echo -e "${GREEN}✓ Mar installed successfully!${NC}"
  echo "  Run: mar --help"
else
  echo -e "${RED}Installation failed. Please add /usr/local/bin to your PATH.${NC}"
  exit 1
fi