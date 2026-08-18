# Contributing

Contributions are welcome. Keep changes small, reviewable, and focused on the project's purpose: reliably showing all Windows 11 tray icons with minimal resource use and minimal system impact.

## Development

1. Create a branch from `main`.
2. Build with CMake 3.20 or newer and a current Visual Studio C++ toolchain, or use `scripts/build.ps1` with MinGW-w64 for x64.
3. Run the self-test and check the GUI process exit code with `Start-Process -Wait -PassThru`.
4. Run `./scripts/test-install.ps1 -Executable <path> -ExpectedVersion <version>` only from an isolated Windows account or CI runner with no existing Tray Icon Promoter installation.
5. Test install, status, live promotion, logon startup, and uninstall on Windows 11.
6. Run `git diff --check` and review the complete diff.
7. Update documentation and `CHANGELOG.md` for user-visible changes. A version bump must also update the expected version in both GitHub Actions workflows.

## Pull requests

- Explain the problem and why the proposed change is the smallest safe solution.
- Do not add telemetry, automatic network updates, elevation, injection, or polling.
- Treat changes to installation, startup, registry access, or release workflows as security-sensitive.
- Keep compiler warnings enabled and resolved.
- Do not commit generated binaries outside a tagged GitHub Release.
- `main` requires a pull request, resolved conversations, and successful `Build x64` and `Build ARM64` checks.
