# Install the SDK required by Chromium 156 without lowering its build checks.
# Source: Microsoft.WindowsSDK.10.0.28000 / 10.0.28000.2526 winget manifest.
$ErrorActionPreference = 'Stop'
$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
$header = Join-Path $sdkRoot 'Include\10.0.28000.0\um\Windows.h'
if (-not (Test-Path $header)) {
    $installer = Join-Path $env:RUNNER_TEMP 'xray-winsdksetup.exe'
    $url = 'https://download.microsoft.com/download/06fc99ac-527e-451e-a536-8866695a2e7e/KIT_BUNDLE_WINDOWSSDK_MEDIACREATION/winsdksetup.exe'
    Invoke-WebRequest -Uri $url -OutFile $installer
    $expected = '02988EA51EAB2A2DB53E19735E51C97A6D221ADA74B9174FA0868870B9403BA0'
    if ((Get-FileHash $installer -Algorithm SHA256).Hash -ne $expected) {
        throw 'Windows SDK installer checksum mismatch'
    }
    $process = Start-Process -FilePath $installer -ArgumentList '/q', '/norestart' -Wait -PassThru
    if ($process.ExitCode -notin @(0, 3010)) {
        throw "Windows SDK installation failed: $($process.ExitCode)"
    }
    Remove-Item $installer
}
if (-not (Test-Path $header)) {
    throw "Required Windows SDK header missing: $header"
}
"Windows SDK ready: $header"
