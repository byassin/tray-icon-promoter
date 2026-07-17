# Contributing

Contributions are welcome. Keep changes small, reviewable, and focused on the project's purpose: reliably showing all Windows 11 tray icons with minimal resource use and minimal system impact.

## Development

1. Create a branch from `main`.
2. Build with CMake and a current Visual Studio toolchain, or use `scripts/build.ps1` with MinGW-w64.
3. Run `TrayIconPromoter.exe --self-test --silent`.
4. Test install, status, live promotion, logon startup, and uninstall on Windows 11.
5. Update documentation and `CHANGELOG.md` for user-visible changes.

## Pull requests

- Explain the problem and why the proposed change is the smallest safe solution.
- Do not add telemetry, automatic network updates, elevation, injection, or polling.
- Treat changes to installation, startup, registry access, or release workflows as security-sensitive.
- Keep compiler warnings enabled and resolved.
- Do not commit generated binaries outside a tagged GitHub Release.

