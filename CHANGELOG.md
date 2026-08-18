# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/byassin/tray-icon-promoter/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/byassin/tray-icon-promoter/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/byassin/tray-icon-promoter/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/byassin/tray-icon-promoter/releases/tag/v1.0.0
