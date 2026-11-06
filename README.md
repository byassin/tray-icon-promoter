# Tray Icon Promoter

[![CI](https://github.com/byassin/tray-icon-promoter/actions/workflows/ci.yml/badge.svg)](https://github.com/byassin/tray-icon-promoter/actions/workflows/ci.yml)
[![Latest release](https://img.shields.io/github/v/release/byassin/tray-icon-promoter)](https://github.com/byassin/tray-icon-promoter/releases/latest)

Tray Icon Promoter keeps **all Windows 11 notification-area icons visible**, including icons that Windows silently moves back into the overflow menu after an application update.

It is a tiny, event-driven Win32 utility. It does not poll, inject code into Explorer, require administrator rights, connect to the internet, or keep PowerShell running in the background.

## Why this exists

Windows 11 stores visibility separately for every registered tray icon. Application updates often create a new registration, so the preference under **Settings > Personalization > Taskbar > Other system tray icons** can reset even when you previously enabled the app.

Tray Icon Promoter watches the current user's notification-icon registry tree. When Windows or an application creates or changes an entry, it sets that entry's `IsPromoted` value to `1` and briefly touches a temporary value so Explorer refreshes immediately.

## Features

- Event-driven: uses `RegNotifyChangeKeyValue`; no polling loop.
- Lightweight: one small native process and zero CPU while idle.
- Per-user: no elevation, service, driver, or scheduled task.
- Self-contained: one executable with no bundled runtime.
- Immediate: refreshes Explorer's per-icon watcher after a visibility change.
- Safe startup: a named mutex prevents duplicate watcher instances.
- Verified repair: installation waits for the old watcher to stop and the replacement watcher to arm notifications and complete its initial scan.
- Actionable status: reports watcher, startup, installed-file, and tray-record health.
- Recovery: reconnects after the notification registry tree is replaced or permissions change.
- Safe updates: staged executable replacement and rollback preserve the previous installation if an update fails.
- Local controls: stop temporarily or restart without changing sign-in preferences.
- Scriptable diagnostics: export JSON with installed version, readiness, counts, and a suggested recovery action.
- Offline and private: no telemetry, analytics, update checker, or network code.
- Publicly buildable: C source, CMake project, CI, release workflow, and checksums are included.

## Install

1. Open the [latest release](https://github.com/byassin/tray-icon-promoter/releases/latest).
2. Download the x64 ZIP for most Intel/AMD Windows PCs, or the ARM64 ZIP for a Windows-on-ARM PC.
3. Optionally download `SHA256SUMS.txt` and verify the package as described below.
4. Extract the ZIP, then double-click `TrayIconPromoter.exe`.

The executable copies itself to:

```text
%LOCALAPPDATA%\TrayIconPromoter\TrayIconPromoter.exe
```

It then adds a per-user startup entry and launches the watcher. Administrator privileges are not required.

To update or repair, run a newly downloaded copy with `--install`. The app stages and backs up the executable before stopping the watcher, then replaces it and verifies readiness. If installation fails, it restores the previous file and startup values and attempts to restart the previous watcher. If recovery cannot complete, the error message identifies the retained recovery executable. Install, uninstall, stop, restart, and self-test operations are serialized.

### Verify a release

`SHA256SUMS.txt` contains hashes for the release ZIP packages. From a PowerShell window in the download directory, display the package hash and compare it with the matching line in the checksum file:

```powershell
$package = Get-Item .\TrayIconPromoter-v*-x64.zip
Get-FileHash -LiteralPath $package.FullName -Algorithm SHA256
Get-Content .\SHA256SUMS.txt
```

Each release also has a GitHub artifact attestation. If [GitHub CLI](https://cli.github.com/) is installed, verify that the package was produced by this repository's release workflow:

```powershell
gh attestation verify $package.FullName `
  --repo byassin/tray-icon-promoter `
  --signer-workflow byassin/tray-icon-promoter/.github/workflows/release.yml `
  --deny-self-hosted-runners
```

### Microsoft Defender SmartScreen

Windows may display an **unrecognized app** warning because the executable is not Authenticode-signed. Code-signing certificates are not currently part of this volunteer project. The source and build workflow are public, and each release includes package checksums and GitHub artifact attestations. An attestation proves which GitHub workflow produced an artifact; it does not replace Windows code signing.

## Commands

Run these from Command Prompt or PowerShell. Add `--silent` to suppress message boxes.

| Command | Purpose |
| --- | --- |
| `TrayIconPromoter.exe` | Installs when run outside the install directory; watches when run from the install directory. |
| `TrayIconPromoter.exe --install` | Installs or repairs the per-user installation. |
| `TrayIconPromoter.exe --watch` | Runs the watcher. Duplicate instances exit immediately. |
| `TrayIconPromoter.exe --once` | Promotes all currently registered tray icons and exits. |
| `TrayIconPromoter.exe --status` | Checks watcher, startup, installed-file, and tray-record health. |
| `TrayIconPromoter.exe --status-json` | Writes JSON diagnostics to standard output. |
| `TrayIconPromoter.exe --stop` | Stops monitoring temporarily; leaves the executable and startup preferences intact. |
| `TrayIconPromoter.exe --restart` | Restarts the installed watcher without changing startup preferences. |
| `TrayIconPromoter.exe --self-test` | Verifies the live event-driven watcher and read-only executable repair using temporary test data. |
| `TrayIconPromoter.exe --help` | Shows supported commands and usage. |
| `TrayIconPromoter.exe --uninstall` | Stops the watcher and removes its startup entry. |

`--status` returns exit code `0` only when the watcher is running and ready, startup is correctly configured and enabled, the installed executable is present and writable, every tray record is promoted, and the registry scan completes without errors. It reports the executing diagnostic version separately from the installed file version, preserves partial counts on scan failure, and suggests a recovery action. With `--silent`, message boxes are suppressed and the exit code remains available to automation.

`--stop` lasts until `--restart`, another watcher launch, or the next sign-in when startup is enabled. It does not hide icons that are already promoted. `--restart` preserves a disabled Startup Apps preference; use Windows Startup Apps to re-enable sign-in startup, or explicitly repair with `--install`.

### Export diagnostics

Use PowerShell to capture the GUI executable's standard output:

```powershell
$check = Start-Process `
  -FilePath "$env:LOCALAPPDATA\TrayIconPromoter\TrayIconPromoter.exe" `
  -ArgumentList '--status-json', '--silent' `
  -RedirectStandardOutput .\status.json `
  -Wait -PassThru
Get-Content .\status.json -Raw | ConvertFrom-Json
$check.ExitCode
```

JSON diagnostics return `0` when healthy, `1` when unhealthy, or `3` if output cannot be written. The report contains versions, installed path, watcher readiness, startup status, aggregate counts, and a recommendation. Review the installed path before sharing it because it can contain your Windows username. Reports contain no per-application icon inventory and are never sent anywhere automatically.

Choose one action per invocation. Unknown arguments, unmatched quotes, oversized arguments, and multiple actions return exit code `2` without installing, uninstalling, or launching a watcher.

For a complete uninstall, run `--uninstall` from a downloaded copy outside `%LOCALAPPDATA%`. That copy can remove the installed executable after the watcher exits. If you run the installed executable itself, Windows keeps that file locked until the command exits; delete `%LOCALAPPDATA%\TrayIconPromoter` afterward.

## Resource usage

The watcher spends nearly all its life blocked inside the Windows registry-notification API. Exact numbers vary by Windows build and measurement tool. The installed public v1.1.0 x64 build measured the following on its Windows 11 verification system:

- `0.0` cumulative CPU seconds at the verification sample;
- approximately 4.6 MiB working set;
- approximately 0.8 MiB private memory;
- one process with three threads and 53 handles as reported by PowerShell.

Measured results for each release should be recorded in its release notes rather than treated as a permanent guarantee.

## Build locally

Builds require Windows and either a Visual Studio C++ toolchain with CMake 3.20 or newer, or a current MinGW-w64 toolchain for the direct PowerShell build helper.

### Visual Studio / Build Tools

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Use `-A ARM64` instead of `-A x64` for an ARM64 cross-build with a Visual Studio toolchain that includes ARM64 components.

### Portable MinGW-w64

Put `gcc.exe` and `windres.exe` on `PATH`, then run:

```powershell
./scripts/build.ps1 -Package
```

The MinGW helper currently builds x64 only. The executable, checksum file, and optional ZIP package are written to `dist`. Its checksum file covers the executable and, when `-Package` is used, the ZIP; public release checksum files cover both architecture ZIPs.

## Test locally

The self-test creates a temporary notification record after watcher startup, resets its promotion value later, and checks both notifications. It also verifies read-only executable replacement. Because this is a Windows GUI executable, use `Start-Process` when a script needs to wait for and inspect its exit code:

```powershell
$test = Start-Process `
  -FilePath .\build\Release\TrayIconPromoter.exe `
  -ArgumentList '--self-test', '--silent' `
  -Wait `
  -PassThru
$test.ExitCode
```

The full install lifecycle test modifies the current user's startup registration and must not be run over a real installation. Use an isolated Windows account or disposable CI runner with no running Tray Icon Promoter instance:

```powershell
.\scripts\test-install.ps1 `
  -Executable .\build\Release\TrayIconPromoter.exe `
  -ExpectedVersion '1.2.0' `
  -FailureExecutable .\build\Release\TrayIconPromoterInstallerTests.exe
```

Run `ctest --test-dir build -C Release --output-on-failure` for parser/JSON/readiness tests, isolated registry deletion/recreation recovery, and atomic replacement/snapshot restoration. Registry tests use unique temporary keys, never the real notification tree. The install lifecycle test also checks failed-startup rollback, stop/restart, startup preferences, JSON output, concurrent-operation rejection, and read-only repair.

## Release process

1. Update the version in `CMakeLists.txt`, `src/tray_icon_promoter.c`, `resources/app.manifest`, `resources/version.rc`, and `CHANGELOG.md`. Run `./scripts/test-version.ps1` to validate alignment; workflows derive their test version from this check.
2. Merge through a pull request only after the required `Build x64` and `Build ARM64` checks pass. The x64 job also runs the self-test, native argument/readiness tests, and install lifecycle test.
3. Tag the merged commit as `vX.Y.Z` and push the tag. Release validation requires an exact match between the tag and project version.
4. GitHub Actions builds x64 and ARM64 packages, creates `SHA256SUMS.txt`, attests the release artifacts, and publishes a GitHub Release.
5. Download the public assets without repository credentials, verify both ZIP hashes and attestations, then install and health-check the published x64 build.

## Security and design boundaries

Tray Icon Promoter is limited to the following per-user state:

- reads tray records and writes `IsPromoted` plus a temporary refresh value under `HKCU\Control Panel\NotifyIconSettings`;
- creates or removes only the `TrayIconPromoter` value under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`;
- reads or removes only the `TrayIconPromoter` value under `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run`;
- creates, stages, backs up, replaces, or removes its own executable under `%LOCALAPPDATA%\TrayIconPromoter`;
- creates and removes temporary files and isolated registry keys during explicitly requested self-tests and developer tests.

It does not modify Explorer binaries, hook other processes, load a DLL into Explorer, request elevation, or contact a server. See [SECURITY.md](SECURITY.md) for vulnerability reporting.

## Limitations

`NotifyIconSettings` and `StartupApproved` are Windows implementation details rather than documented long-term configuration contracts. A future Windows update could change them. The utility deliberately fails quietly and retries when the notification registry tree is temporarily unavailable.

This tool always promotes every registered icon. It is intentionally not an icon-by-icon rules engine.

GitHub's x64 Windows runner executes the self-test, native argument/readiness tests, and install lifecycle test. CI cross-builds and packages ARM64, but does not execute the ARM64 binary on native ARM64 hardware.

See [the v1.2 review](docs/REVIEW-v1.2.0.md) for the reviewed issues, changes, and regression coverage.

## License

[MIT](LICENSE)
