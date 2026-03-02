# Mar Language Installer for Windows
# Run in PowerShell as Administrator:
#   irm https://github.com/MAdityaRao/mar-lang/releases/download/v1.5.3/install.ps1 | iex

$VERSION = "v1.5.4"
$REPO = "MAdityaRao/mar-lang"
$URL = "https://github.com/$REPO/releases/download/$VERSION/mar-windows-x64.exe"
$DEST = "$env:ProgramFiles\Mar"
$BIN = "$DEST\mar.exe"

Write-Host "Mar Language Installer" -ForegroundColor White
Write-Host "Version : $VERSION" -ForegroundColor Cyan
Write-Host "Platform: Windows x64" -ForegroundColor Cyan
Write-Host ""

# Create install dir
if (!(Test-Path $DEST)) {
  New-Item -ItemType Directory -Path $DEST | Out-Null
}

Write-Host "Downloading..."
Invoke-WebRequest -Uri $URL -OutFile $BIN

# Add to PATH if not already there
$CurrentPath = [Environment]::GetEnvironmentVariable("Path", "Machine")
if ($CurrentPath -notlike "*$DEST*") {
  [Environment]::SetEnvironmentVariable("Path", "$CurrentPath;$DEST", "Machine")
  Write-Host "Added $DEST to system PATH." -ForegroundColor Yellow
  Write-Host "Please restart your terminal for PATH changes to take effect."
}

Write-Host ""
Write-Host "✓ Mar installed successfully!" -ForegroundColor Green
Write-Host "  Run: mar --help"