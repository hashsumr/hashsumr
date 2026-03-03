@echo off

@SET /P version="Enter the version number to be packed: "
@SET /P password="Enter your PFX certificate password: "

@REM download binaries
curl -L -o "msix\app\hashsumr-amd64.exe" "https://github.com/hashsumr/hashsumr/releases/download/v%version%/hashsumr-amd64.exe"
curl -L -o "msix\app\hashsumr-arm64.exe" "https://github.com/hashsumr/hashsumr/releases/download/v%version%/hashsumr-arm64.exe"

@REM set version
powershell -Command "(Get-Content .\msix\app\AppxManifest_amd64.xml) -replace '\$version\$', '%version%.0' | Set-Content .\msix\app\AppxManifest_amd64.xml"
powershell -Command "(Get-Content .\msix\app\AppxManifest_arm64.xml) -replace '\$version\$', '%version%.0' | Set-Content .\msix\app\AppxManifest_arm64.xml"

@REM packaging
mkdir output
makeappx pack /f .\msix\map_amd64.txt /p .\output\hashsumr_amd64.msix
makeappx pack /f .\msix\map_arm64.txt /p .\output\hashsumr_arm64.msix

@REM sign the msix
signtool sign /fd SHA256 /a /f ".\cert\chunying-dev.pfx" /p "%password%" .\output\hashsumr_amd64.msix
signtool sign /fd SHA256 /a /f ".\cert\chunying-dev.pfx" /p "%password%" .\output\hashsumr_arm64.msix

@REM create and sign the bundle
makeappx bundle /f .\msix\bundlemap.txt /p .\output\hashsumr.msixbundle /bv "%version%.0"
signtool sign /fd SHA256 /a /f ".\cert\chunying-dev.pfx" /p "%password%" .\output\hashsumr.msixbundle
