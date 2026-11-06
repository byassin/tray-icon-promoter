# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Documentation

- Updated resource measurements, troubleshooting/exit-code guidance, and PowerShell/checksum examples for v1.2.0.
- Published updated documentation separately from the original attested v1.2.0 application packages.

## [1.2.0] - 2026-11-06

### Added

- Temporary `--stop` and preference-preserving `--restart` commands.
- `--status-json` diagnostics with actual installed version, watcher readiness, partial scan counts, and a recovery recommendation.
- Isolated registry recovery, atomic installer, and lifecycle rollback/operation-lock/diagnostic regression tests.
- Shared version validation that prevents publishing a tag with mismatched executable metadata.

### Fixed

- Persistent registry enumeration errors terminate scans instead of hanging the watcher or status command.
- Watcher readiness resets during recovery; registry permission changes trigger a rescan and retained stop events no longer abort a fresh watcher.
- Installer stages replacements before stopping the watcher and restores previous executable/startup state on failure. Concurrent lifecycle operations and self-tests are serialized.
- Uninstall handles read-only files, reports deletion failures, and succeeds when already uninstalled.
- Diagnostic scans retain error counts instead of replacing failed scans with zeroes.
- Lifecycle tests refuse all existing startup values, including empty or disabled registrations.
- Portable package checksums include the ZIP; CI self-tests and jobs have bounded runtimes.

## [1.1.1] - 2026-11-05

### Fixed

- Install waits for explicit watcher readiness after notifications are armed and the first scan succeeds, rejects an exited child, and cleans up its child on startup failure or timeout.
- The self-test now verifies new records and later value changes after startup, rather than relying on the initial scan.
- Unknown, conflicting, oversized, and malformed command-line arguments return exit code 2 without performing the default action.

### Added

- `--help` displays command usage.
- Native readiness and parser regression tests, plus failed-startup and command-side-effect checks in the install lifecycle test.

### Changed

- Updated artifact upload/download actions to v7.0.2/v8.0.2.
- Clarified architecture-specific installation, package checksum and attestation verification, status health criteria, build and test prerequisites, supported security versions, ARM64 CI coverage, and the exact per-user state the application modifies.

## [1.1.0] - 2026-08-17

### Added

- Status diagnostics for startup registration, Startup Apps enablement, the installed path, and read-only install state.
- GitHub artifact attestations for release packages and their checksum manifest.
- An end-to-end CI test covering install, disabled startup detection, read-only repair, watcher restart, status, and uninstall.

### Changed

- Install and repair now confirm that the old watcher exits and the replacement watcher acquires its singleton mutex before reporting success.
- Install and uninstall clear stale per-user Startup Apps overrides for Tray Icon Promoter.
- The self-test now exercises the real event-driven watcher loop instead of calling the promotion routine directly.
- GitHub Actions dependencies were updated to their current Node.js 24 releases and are now monitored by Dependabot.

### Fixed

- Install no longer reports success when the replacement watcher fails to start.
- Status no longer reports a healthy installation when startup is missing, disabled, or points at the wrong executable.

## [1.0.1] - 2026-08-05

### Fixed

- Repair installs now replace an existing executable when its read-only attribute is set.

## [1.0.0] - 2026-07-16

### Added

- Native, event-driven Windows 11 tray-icon watcher.
- Per-user installation and startup registration without elevation.
- Immediate Explorer refresh after promoting an icon.
- Install, uninstall, watch, one-shot, status, and self-test commands.
- x64 and ARM64 CI/release builds with SHA-256 checksums.

[Unreleased]: https://github.com/byassin/tray-icon-promoter/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/byassin/tray-icon-promoter/compare/v1.1.1...v1.2.0
[1.1.1]: https://github.com/byassin/tray-icon-promoter/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/byassin/tray-icon-promoter/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/byassin/tray-icon-promoter/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/byassin/tray-icon-promoter/releases/tag/v1.0.0
