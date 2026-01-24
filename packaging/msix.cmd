@echo off

@SET /P password="Enter your PFX certificate password: "

@REM download binaries
curl -L -o "msix\hashsumr-amd64.exe" "https://github.com/hashsumr/hashsumr/releases/download/v0.0.2/hashsumr-amd64.exe"
curl -L -o "msix\hashsumr-arm64.exe" "https://github.com/hashsumr/hashsumr/releases/download/v0.0.2/hashsumr-arm64.exe"

@REM packaging
makeappx pack /f map_amd64.txt /p hashsumr_amd64.msix
makeappx pack /f map_arm64.txt /p hashsumr_arm64.msix

@REM sign the msix
signtool sign /fd SHA256 /a /f ".\hashsumr-dev.pfx" /p "%password%" hashsumr_amd64.msix
signtool sign /fd SHA256 /a /f ".\hashsumr-dev.pfx" /p "%password%" hashsumr_arm64.msix

@REM create and sign the bundle
makeappx bundle /f bundlemap.txt /p hashsumr.msixbundle
signtool sign /fd SHA256 /a /f ".\hashsumr-dev.pfx" /p "%password%" hashsumr.msixbundle
