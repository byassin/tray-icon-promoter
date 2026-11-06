[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Executable,

    [Parameter(Mandatory)]
    [string]$FailureExecutable,

    [Parameter(Mandatory)]
    [string]$ExpectedVersion
)

$ErrorActionPreference = 'Stop'
$sourceExecutable = (Resolve-Path -LiteralPath $Executable).Path
$failureExecutablePath = (Resolve-Path -LiteralPath $FailureExecutable).Path
$runKeyPath = 'Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run'
$startupApprovedKeyPath = 'Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run'
$runValueName = 'TrayIconPromoter'
$originalLocalAppData = $env:LOCALAPPDATA
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("TrayIconPromoter Install Test Ω-{0}" -f [Guid]::NewGuid().ToString('N'))
$installedExecutable = Join-Path $testRoot 'TrayIconPromoter\TrayIconPromoter.exe'
$expectedRunValue = '"{0}" --watch' -f $installedExecutable

function Invoke-GuiProcess {
    param(
        [Parameter(Mandatory)]
        [string]$FilePath,

        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    $process = Start-Process -WindowStyle Hidden -FilePath $FilePath -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit(15000)) {
        $process.Kill($true)
        throw "$FilePath did not exit within 15 seconds."
    }
    return $process.ExitCode
}

function Get-StatusReport {
    param(
        [Parameter(Mandatory)]
        [string]$FilePath,

        [Parameter(Mandatory)]
        [int]$ExpectedExit
    )

    $reportPath = Join-Path $testRoot 'status.json'
    $process = Start-Process -WindowStyle Hidden -FilePath $FilePath `
        -ArgumentList '--status-json', '--silent' -RedirectStandardOutput $reportPath -PassThru
    if (-not $process.WaitForExit(15000)) {
        $process.Kill($true)
        throw 'JSON status did not exit within 15 seconds.'
    }
    if ($process.ExitCode -ne $ExpectedExit) {
        throw "JSON status returned $($process.ExitCode) instead of $ExpectedExit."
    }
    return Get-Content -LiteralPath $reportPath -Raw -Encoding utf8 | ConvertFrom-Json
}

function Test-AppStartupValue {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    return $runValueName -in (Get-Item -LiteralPath $Path).GetValueNames()
}

if (Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue) {
    throw 'Refusing to run the isolated install test while a Tray Icon Promoter watcher is active.'
}

foreach ($startupPath in @($runKeyPath, $startupApprovedKeyPath)) {
    if (Test-AppStartupValue $startupPath) {
        throw "Refusing to overwrite existing Tray Icon Promoter startup state in $startupPath."
    }
}

New-Item -ItemType Directory -Path $testRoot | Out-Null
$env:LOCALAPPDATA = $testRoot

try {
    foreach ($arguments in @(
        @('--help', '--silent'),
        @('--staus', '--silent'),
        @('--install', '--uninstall', '--silent')
    )) {
        $expectedExit = if ($arguments[0] -eq '--help') { 0 } else { 2 }
        $actualExit = Invoke-GuiProcess -FilePath $sourceExecutable -Arguments $arguments
        if ($actualExit -ne $expectedExit -or (Test-Path -LiteralPath $installedExecutable)) {
            throw 'Help or invalid arguments caused an unexpected exit or installation side effect.'
        }
    }

    $setupLock = [System.Threading.Mutex]::new($true, 'Local\TrayIconPromoter.Setup-8D37B978-83D7-4E36-A39C-F08BDB8B5911')
    try {
        $blockedInstallExit = Invoke-GuiProcess -FilePath $sourceExecutable -Arguments @('--install', '--silent')
        if ($blockedInstallExit -ne 26 -or (Test-Path -LiteralPath $installedExecutable) -or
            (Test-AppStartupValue $runKeyPath) -or (Test-AppStartupValue $startupApprovedKeyPath)) {
            throw 'A blocked install failed to time out without changing installation state.'
        }
    }
    finally {
        $setupLock.ReleaseMutex()
        $setupLock.Dispose()
    }

    # A mutex with the stop-event name makes the child fail initialization.
    # The installer must observe the child exit, rather than its temporary mutex.
    $collision = [System.Threading.Mutex]::new($false, 'Local\TrayIconPromoter.Stop-8D37B978-83D7-4E36-A39C-F08BDB8B5911')
    try {
        $failedInstallExit = Invoke-GuiProcess -FilePath $sourceExecutable -Arguments @('--install', '--silent')
        if ($failedInstallExit -ne 24) {
            throw "Failed watcher startup returned $failedInstallExit instead of 24."
        }
        if (Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue) {
            throw 'Failed watcher startup left a child process behind.'
        }
        if ((Test-Path -LiteralPath $installedExecutable) -or
            (Test-AppStartupValue $runKeyPath) -or (Test-AppStartupValue $startupApprovedKeyPath)) {
            throw 'A failed fresh install left an executable or startup registration behind.'
        }
    }
    finally {
        $collision.Dispose()
    }

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
    $ready = [System.Threading.EventWaitHandle]::OpenExisting('Local\TrayIconPromoter.Ready-8D37B978-83D7-4E36-A39C-F08BDB8B5911')
    try {
        if (-not $ready.WaitOne(0)) { throw 'Install returned before watcher readiness.' }
    }
    finally { $ready.Dispose() }

    $report = Get-StatusReport -FilePath $installedExecutable -ExpectedExit 0
    if (-not $report.healthy -or -not $report.watcher_running -or -not $report.watcher_ready -or
        -not $report.installed_writable -or -not $report.scan_complete -or
        $report.version -ne $ExpectedVersion -or $report.installed_version -ne $ExpectedVersion -or
        $report.installed_path -ne $installedExecutable -or $report.startup -ne 'configured' -or
        $report.records -ne $report.promoted -or $report.errors -ne 0) {
        throw 'JSON status did not describe the healthy installation accurately.'
    }

    foreach ($attempt in 1..2) {
        $stopExitCode = Invoke-GuiProcess -FilePath $installedExecutable -Arguments @('--stop', '--silent')
        if ($stopExitCode -ne 0 -or (Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue)) {
            throw 'Stop failed or was not idempotent.'
        }
    }
    if (-not (Test-Path -LiteralPath $installedExecutable) -or
        (Get-ItemProperty -LiteralPath $runKeyPath -Name $runValueName).$runValueName -ne $expectedRunValue) {
        throw 'Stop removed the installed file or changed startup registration.'
    }
    $report = Get-StatusReport -FilePath $installedExecutable -ExpectedExit 1
    if ($report.healthy -or $report.watcher_running -or $report.watcher_ready -or $report.startup -ne 'configured') {
        throw 'JSON status did not report the stopped watcher.'
    }
    $restartExitCode = Invoke-GuiProcess -FilePath $installedExecutable -Arguments @('--restart', '--silent')
    if ($restartExitCode -ne 0 -or @(Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue).Count -ne 1) {
        throw 'Restart did not leave exactly one watcher running.'
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
    $disabledOverride = [byte[]](Get-ItemProperty -LiteralPath $startupApprovedKeyPath -Name $runValueName).$runValueName
    $restartExitCode = Invoke-GuiProcess -FilePath $installedExecutable -Arguments @('--restart', '--silent')
    $afterRestartOverride = [byte[]](Get-ItemProperty -LiteralPath $startupApprovedKeyPath -Name $runValueName).$runValueName
    if ($restartExitCode -ne 0 -or
        [Convert]::ToBase64String($disabledOverride) -ne [Convert]::ToBase64String($afterRestartOverride) -or
        (Get-ItemProperty -LiteralPath $runKeyPath -Name $runValueName).$runValueName -ne $expectedRunValue) {
        throw 'Restart changed the disabled startup preference or failed.'
    }
    $report = Get-StatusReport -FilePath $installedExecutable -ExpectedExit 1
    if ($report.healthy -or -not $report.watcher_running -or -not $report.watcher_ready -or
        $report.startup -ne 'disabled in Startup Apps') {
        throw 'JSON status did not report disabled startup accurately after restart.'
    }

    $stopExitCode = Invoke-GuiProcess -FilePath $installedExecutable -Arguments @('--stop', '--silent')
    if ($stopExitCode -ne 0) { throw 'Could not stop the watcher before rollback testing.' }

    # A PE overlay makes the previous executable distinguishable without changing
    # its runnable code, resources, or version. These are isolated test files only.
    $overlay = [System.Text.Encoding]::ASCII.GetBytes('TrayIconPromoter previous-install rollback fixture')
    $stream = [System.IO.File]::Open($installedExecutable, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write)
    try { $stream.Write($overlay, 0, $overlay.Length) }
    finally { $stream.Dispose() }
    Set-ItemProperty -LiteralPath $installedExecutable -Name IsReadOnly -Value $true
    $previousAttributes = (Get-Item -LiteralPath $installedExecutable).Attributes
    $previousHash = (Get-FileHash -LiteralPath $installedExecutable -Algorithm SHA256).Hash
    $previousRunValue = $expectedRunValue.Replace('--watch', '--WATCH')
    Set-ItemProperty -LiteralPath $runKeyPath -Name $runValueName -Value $previousRunValue
    $collision = [System.Threading.Mutex]::new($false, 'Local\TrayIconPromoter.Stop-8D37B978-83D7-4E36-A39C-F08BDB8B5911')
    try {
        $rollbackExit = Invoke-GuiProcess -FilePath $sourceExecutable -Arguments @('--install', '--silent')
        $restoredApproval = [byte[]](Get-ItemProperty -LiteralPath $startupApprovedKeyPath -Name $runValueName).$runValueName
        if ($rollbackExit -ne 24 -or
            (Get-FileHash -LiteralPath $installedExecutable -Algorithm SHA256).Hash -ne $previousHash -or
            (Get-Item -LiteralPath $installedExecutable).Attributes -ne $previousAttributes -or
            (Get-ItemProperty -LiteralPath $runKeyPath -Name $runValueName).$runValueName -cne $previousRunValue -or
            [Convert]::ToBase64String($restoredApproval) -ne [Convert]::ToBase64String($disabledOverride) -or
            (Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue)) {
            throw 'Failed update did not restore the previous executable, attributes, startup values, and stopped state.'
        }
    }
    finally { $collision.Dispose() }

    # The console-only test harness runs the real installer, but exits immediately
    # when its replacement is launched with --watch. This exercises rollback of
    # a failed update while the prior production watcher was actually running.
    $restartExitCode = Invoke-GuiProcess -FilePath $installedExecutable -Arguments @('--restart', '--silent')
    if ($restartExitCode -ne 0 -or @(Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue).Count -ne 1) {
        throw 'Could not start the prior installation before running-watcher rollback testing.'
    }
    $runningRollbackExit = Invoke-GuiProcess -FilePath $failureExecutablePath -Arguments @('--failed-update')
    $restoredApproval = [byte[]](Get-ItemProperty -LiteralPath $startupApprovedKeyPath -Name $runValueName).$runValueName
    if ($runningRollbackExit -ne 24 -or
        (Get-FileHash -LiteralPath $installedExecutable -Algorithm SHA256).Hash -ne $previousHash -or
        (Get-Item -LiteralPath $installedExecutable).Attributes -ne $previousAttributes -or
        (Get-ItemProperty -LiteralPath $runKeyPath -Name $runValueName).$runValueName -cne $previousRunValue -or
        [Convert]::ToBase64String($restoredApproval) -ne [Convert]::ToBase64String($disabledOverride) -or
        @(Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue).Count -ne 1) {
        throw 'Failed running update did not restore the prior executable, attributes, startup values, and watcher.'
    }
    $ready = [System.Threading.EventWaitHandle]::OpenExisting('Local\TrayIconPromoter.Ready-8D37B978-83D7-4E36-A39C-F08BDB8B5911')
    try {
        if (-not $ready.WaitOne(0)) { throw 'The restored watcher was not ready after running-update rollback.' }
    }
    finally { $ready.Dispose() }
    $report = Get-StatusReport -FilePath $installedExecutable -ExpectedExit 1
    if ($report.healthy -or -not $report.watcher_running -or -not $report.watcher_ready -or
        $report.startup -ne 'disabled in Startup Apps' -or $report.installed_writable) {
        throw 'JSON status did not describe the restored read-only, startup-disabled installation.'
    }

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
    $repeatUninstallExit = Invoke-GuiProcess -FilePath $sourceExecutable -Arguments @('--uninstall', '--silent')
    if ($repeatUninstallExit -ne 0 -or
        (Test-Path -LiteralPath (Split-Path -Parent $installedExecutable)) -or
        (Test-AppStartupValue $runKeyPath) -or (Test-AppStartupValue $startupApprovedKeyPath)) {
        throw 'Repeated uninstall was not idempotent or left installation state behind.'
    }

    Write-Output 'Install, rollback, JSON diagnostics, stop/restart, startup preferences, read-only repair, and repeated uninstall lifecycle test passed.'
}
finally {
    try {
        if (Get-Process -Name TrayIconPromoter -ErrorAction SilentlyContinue) {
            Invoke-GuiProcess -FilePath $sourceExecutable -Arguments @('--uninstall', '--silent') | Out-Null
        }
    }
    finally {
        Remove-ItemProperty -LiteralPath $runKeyPath -Name $runValueName -ErrorAction SilentlyContinue
        Remove-ItemProperty -LiteralPath $startupApprovedKeyPath -Name $runValueName -ErrorAction SilentlyContinue
        $env:LOCALAPPDATA = $originalLocalAppData
        $resolvedTestRoot = [System.IO.Path]::GetFullPath($testRoot)
        $temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if (-not $resolvedTestRoot.StartsWith($temporaryRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean an install test directory outside the temporary directory: $resolvedTestRoot"
        }
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
