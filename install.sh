#!/bin/bash
set -e

VERSION="v1.0.1"
BASE="https://github.com/MAdityaRao/mar-lang/releases/download/$VERSION"

OS=$(uname -s)
ARCH=$(uname -m)

if [ "$OS" = "Darwin" ] && [ "$ARCH" = "arm64" ]; then
    BINARY="mar-macos-arm64"
elif [ "$OS" = "Darwin" ]; then
    BINARY="mar-macos-x86_64"
elif [ "$OS" = "Linux" ]; then
    BINARY="mar-linux-x86_64"
else
    echo "Unsupported OS: $OS"
    echo "Windows users: install WSL first, then re-run this script."
    exit 1
fi
echo 'function runmar() { mar "$1" -o /tmp/mar_out.c && cc -o /tmp/mar_out /tmp/mar_out.c && /tmp/mar_out }' >> ~/.zshrc
echo 'function runmar() { mar "$1" -o /tmp/mar_out.c && cc -o /tmp/mar_out /tmp/mar_out.c && /tmp/mar_out }' >> ~/.bash_profile
echo "Installing Mar $VERSION for $OS/$ARCH..."
curl -L "$BASE/$BINARY" -o mar_tmp
chmod +x mar_tmp
sudo mv mar_tmp /usr/local/bin/mar
echo "Done! Run: mar --help"