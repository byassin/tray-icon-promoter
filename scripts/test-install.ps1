[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Executable,

    [Parameter(Mandatory)]
    [string]$ExpectedVersion
)

$ErrorActionPreference = 'Stop'
$sourceExecutable = (Resolve-Path -LiteralPath $Executable).Path
$runKeyPath = 'Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run'
$startupApprovedKeyPath = 'Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run'
$runValueName = 'TrayIconPromoter'
$originalLocalAppData = $env:LOCALAPPDATA
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("TrayIconPromoterInstallTest-{0}" -f [Guid]::NewGuid().ToString('N'))
$installedExecutable = Join-Path $testRoot 'TrayIconPromoter\TrayIconPromoter.exe'
$expectedRunValue = '"{0}" --watch' -f $installedExecutable

function Invoke-GuiProcess {
    param(
        [Parameter(Mandatory)]
        [string]$FilePath,

        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit(15000)) {
        $process.Kill()
        throw "$FilePath did not exit within 15 seconds."
    }
    return $process.ExitCode
}

if (Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue) {
    throw 'Refusing to run the isolated install test while a Tray Icon Promoter watcher is active.'
}

if ((Get-ItemProperty -LiteralPath $runKeyPath -Name $runValueName -ErrorAction SilentlyContinue).$runValueName) {
    throw 'Refusing to overwrite an existing Tray Icon Promoter startup registration.'
}

New-Item -ItemType Directory -Path $testRoot | Out-Null
$env:LOCALAPPDATA = $testRoot

try {
    $installExitCode = Invoke-GuiProcess -FilePath $sourceExecutable -Arguments @('--install', '--silent')
    if ($installExitCode -ne 0) {
        throw "Initial install failed with exit code $installExitCode."
    }

    $installedItem = Get-Item -LiteralPath $installedExecutable
    if ($installedItem.VersionInfo.FileVersion -ne $ExpectedVersion) {
        throw "Installed version $($installedItem.VersionInfo.FileVersion) does not match $ExpectedVersion."
    }

    $actualRunValue = (Get-ItemProperty -LiteralPath $runKeyPath -Name $runValueName).$runValueName
    if ($actualRunValue -ne $expectedRunValue) {
        throw "Unexpected startup command: $actualRunValue"
    }

    if (@(Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue).Count -ne 1) {
        throw 'Initial install did not leave exactly one watcher running.'
    }

    New-Item -Path $startupApprovedKeyPath -Force | Out-Null
    New-ItemProperty `
        -LiteralPath $startupApprovedKeyPath `
        -Name $runValueName `
        -PropertyType Binary `
        -Value ([byte[]](3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) `
        -Force | Out-Null

    $disabledStatusExitCode = Invoke-GuiProcess -FilePath $installedExecutable -Arguments @('--status', '--silent')
    if ($disabledStatusExitCode -eq 0) {
        throw 'Status did not report a disabled Startup Apps registration.'
    }

    Set-ItemProperty -LiteralPath $installedExecutable -Name IsReadOnly -Value $true
    $repairExitCode = Invoke-GuiProcess -FilePath $sourceExecutable -Arguments @('--install', '--silent')
    if ($repairExitCode -ne 0) {
        throw "Read-only repair failed with exit code $repairExitCode."
    }

    $installedItem = Get-Item -LiteralPath $installedExecutable
    if ($installedItem.IsReadOnly) {
        throw 'Repair left the installed executable read-only.'
    }

    $sourceHash = (Get-FileHash -LiteralPath $sourceExecutable -Algorithm SHA256).Hash
    $installedHash = (Get-FileHash -LiteralPath $installedExecutable -Algorithm SHA256).Hash
    if ($sourceHash -ne $installedHash) {
        throw 'Repair did not install the tested executable.'
    }

    if ((Get-ItemProperty -LiteralPath $startupApprovedKeyPath -Name $runValueName -ErrorAction SilentlyContinue).$runValueName) {
        throw 'Repair did not clear the disabled Startup Apps override.'
    }

    if (@(Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue).Count -ne 1) {
        throw 'Repair did not leave exactly one watcher running.'
    }

    $statusExitCode = Invoke-GuiProcess -FilePath $installedExecutable -Arguments @('--status', '--silent')
    if ($statusExitCode -ne 0) {
        throw "Healthy installed status returned exit code $statusExitCode."
    }

    $uninstallExitCode = Invoke-GuiProcess -FilePath $sourceExecutable -Arguments @('--uninstall', '--silent')
    if ($uninstallExitCode -ne 0) {
        throw "Uninstall failed with exit code $uninstallExitCode."
    }

    if (Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue) {
        throw 'Uninstall left the watcher running.'
    }

    if (Test-Path -LiteralPath $installedExecutable) {
        throw 'Uninstall left the installed executable behind.'
    }

    if ((Get-ItemProperty -LiteralPath $runKeyPath -Name $runValueName -ErrorAction SilentlyContinue).$runValueName) {
        throw 'Uninstall left the startup registration behind.'
    }

    Write-Output 'Install, read-only repair, status, and uninstall lifecycle test passed.'
}
finally {
    if (Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue) {
        Invoke-GuiProcess -FilePath $sourceExecutable -Arguments @('--uninstall', '--silent') | Out-Null
    }
    Remove-ItemProperty -LiteralPath $runKeyPath -Name $runValueName -ErrorAction SilentlyContinue
    Remove-ItemProperty -LiteralPath $startupApprovedKeyPath -Name $runValueName -ErrorAction SilentlyContinue
    $env:LOCALAPPDATA = $originalLocalAppData
    Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
}
