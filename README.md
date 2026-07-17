# Tray Icon Promoter

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
- Offline and private: no telemetry, analytics, update checker, or network code.
- Publicly buildable: C source, CMake project, CI, release workflow, and checksums are included.

## Install

1. Download the executable for your system from the repository's **Releases** page.
2. Verify the SHA-256 checksum if desired.
3. Double-click `TrayIconPromoter.exe`.

The executable copies itself to:

```text
%LOCALAPPDATA%\TrayIconPromoter\TrayIconPromoter.exe
```

It then adds a per-user startup entry and launches the watcher. Administrator privileges are not required.

### Microsoft Defender SmartScreen

Early releases may display an **unrecognized app** warning because the binary is not code-signed. Code-signing certificates cost money and are not currently part of this volunteer project. The source and reproducible build workflow are available for inspection; release checksums are published alongside every binary.

## Commands

Run these from Command Prompt or PowerShell. Add `--silent` to suppress message boxes.

| Command | Purpose |
| --- | --- |
| `TrayIconPromoter.exe` | Installs when run outside the install directory; watches when run from the install directory. |
| `TrayIconPromoter.exe --install` | Installs or repairs the per-user installation. |
| `TrayIconPromoter.exe --watch` | Runs the watcher. Duplicate instances exit immediately. |
| `TrayIconPromoter.exe --once` | Promotes all currently registered tray icons and exits. |
| `TrayIconPromoter.exe --status` | Shows watcher state and registry counts. |
| `TrayIconPromoter.exe --self-test` | Creates an isolated temporary registry entry, verifies promotion, and removes it. |
| `TrayIconPromoter.exe --uninstall` | Stops the watcher and removes its startup entry. |

For a complete uninstall, run `--uninstall` from a downloaded copy outside `%LOCALAPPDATA%`. That copy can remove the installed executable after the watcher exits. If you run the installed executable itself, Windows keeps that file locked until the command exits; delete `%LOCALAPPDATA%\TrayIconPromoter` afterward.

## Resource usage

The watcher spends nearly all its life blocked inside the Windows registry-notification API. Exact numbers vary by Windows build and measurement tool. Version 1.0.0 measured the following on its original Windows 11 test system:

- zero measurable idle CPU;
- approximately 4.1 MB working set;
- approximately 0.7 MB private memory;
- one steady-state process thread and 52 handles as reported by PowerShell.

Measured results for each release should be recorded in its release notes rather than treated as a permanent guarantee.

## Build locally

### Visual Studio / Build Tools

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

### Portable MinGW-w64

Put `gcc.exe` and `windres.exe` on `PATH`, then run:

```powershell
./scripts/build.ps1 -Package
```

The executable, checksum file, and optional ZIP package are written to `dist`.

## Release process

1. Update the version in `CMakeLists.txt`, `src/tray_icon_promoter.c`, `resources/version.rc`, and `CHANGELOG.md`.
2. Merge only after the Windows CI build and x64 self-test pass.
3. Tag the commit, for example `v1.0.0`, and push the tag.
4. GitHub Actions builds x64 and ARM64 packages, creates `SHA256SUMS.txt`, and publishes a GitHub Release.

## Security and design boundaries

Tray Icon Promoter only reads and writes:

- `HKCU\Control Panel\NotifyIconSettings`
- its value under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
- its own directory under `%LOCALAPPDATA%`

It does not modify Explorer binaries, hook other processes, load a DLL into Explorer, request elevation, or contact a server. See [SECURITY.md](SECURITY.md) for vulnerability reporting.

## Limitations

`NotifyIconSettings` is an implementation detail of Windows 11 rather than a documented long-term configuration contract. A future Windows update could change it. The utility deliberately fails quietly and retries when the registry tree is temporarily unavailable.

This tool always promotes every registered icon. It is intentionally not an icon-by-icon rules engine.

## License

[MIT](LICENSE)
