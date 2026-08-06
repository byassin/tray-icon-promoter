# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Repair installs now replace an existing executable when its read-only attribute is set.

## [1.0.0] - 2026-07-16

### Added

- Native, event-driven Windows 11 tray-icon watcher.
- Per-user installation and startup registration without elevation.
- Immediate Explorer refresh after promoting an icon.
- Install, uninstall, watch, one-shot, status, and self-test commands.
- x64 and ARM64 CI/release builds with SHA-256 checksums.

[Unreleased]: https://github.com/byassin/tray-icon-promoter/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/byassin/tray-icon-promoter/releases/tag/v1.0.0
