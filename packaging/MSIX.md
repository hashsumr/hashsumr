# Notes on Packaging MSIX

## Directory Structure
```
msix/app
├─ AppxManifest.xml
├─ Assets/
│  ├─ StoreLogo.png (recommended: 100x100)
│  ├─ Square44x44Logo.png (recommended: 88x88)
│  └─ Square150x150Logo.png (recommended: 300x300)
├─ hashsumr-amd64.exe
└─ hashsumr-arm64.exe
```

## Create Signing Certificate (PowerShell)

1. Create the certificate in your personal store
```
$cert = New-SelfSignedCertificate -Type CodeSigningCert `
    -Subject "CN=613656AF-462E-4804-97D4-06B846E006FE" `
    -HashAlgorithm sha256 `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3") # Key usage for Code Signing
```

2. Export the certificate to a file
```
$certPath = ".\cert\hashsumr-dev.cer"
Export-Certificate -Cert $cert -FilePath $certPath
```

3. Trust the certificate on your machine (need admin privilege)
```
Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\TrustedPeople"
```

4. Export the certificate to the `.pfx` format
   - Run `certmgr.msc`
   - Find the hashsumr certificate in `Certificate - Current User > Personal > Certificates`
   - Right-click `hashsumr` and choose `All Tasks > Export ...`
   - Follow the instructions
     - Yes, export the private key
     - Personal Information Exchange - PKCS #12 (.PFX)
     - Set a password, e.g., `VerySecurePassword`, and choose a prefered Encryption method 
     - Save the .pfx file, e.g., `hashsumr-dev.pfx`

## Create and Sign the Package

- Modify the package version and certificate file based on your setup.
- Run the `msix.cmd` command. You may need to run this command in the command line prompt of "Developer Command Prompt for VS."

## Install the Package (PowerShell)
```
Add-AppxPackage -Path .\output\hashsumr.msixbundle
```

## List and Uninstall the Package (PowerShell)
```
Get-AppxPackage *hashsumr*
Remove-AppxPackage <FullPackageName>
```
