#!/bin/bash
set -e

VERSION="v1.1.15"
BASE="https://github.com/MAdityaRao/mar-lang/releases/download/$VERSION"

OS=$(uname -s)
ARCH=$(uname -m)

if [ "$OS" = "Darwin" ] && [ "$ARCH" = "arm64" ]; then
    BINARY="mar-macos-arm64"
elif [ "$OS" = "Darwin" ]; then
    BINARY="mar-macos-x86_64"
elif [ "$OS" = "Linux" ]; then
    BINARY="mar-linux-x86_64"
elif [[ "$OS" == *"MINGW"* ]] || [[ "$OS" == *"MSYS"* ]]; then
    BINARY="mar-windows-x64.exe"
    # ... logic to move to a Windows path or suggest adding to PATH
else
    echo "Unsupported OS: $OS"
    echo "Windows users: install WSL first, then re-run this script."
    exit 1
fi
echo "Installing Mar $VERSION for $OS/$ARCH..."
curl -L "$BASE/$BINARY" -o mar_tmp
chmod +x mar_tmp
sudo mv mar_tmp /usr/local/bin/mar
echo "Done! Run: mar --help"