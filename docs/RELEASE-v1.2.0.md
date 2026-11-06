## New controls and diagnostics

- `--stop` temporarily stops monitoring without removing the executable or changing startup preferences.
- `--restart` restarts the installed watcher and preserves disabled Startup Apps preferences.
- `--status-json` exports local JSON diagnostics through standard output. Status now distinguishes installed and executing versions, current readiness, partial scans/errors, and a recommended recovery action.

## Reliability improvements

- Fixed a potential endless scan when Windows deletes/replaces the tray registry tree. Monitoring now recovers and reports readiness accurately, including permission changes.
- Updates stage replacements and back up the previous executable/startup values before stopping the watcher. Failures roll back and attempt to restart the previous watcher; incomplete recovery retains a backup and reports its location.
- Lifecycle operations and self-tests are serialized. Uninstall handles read-only files and repeated calls and reports removal failures.
- Version validation prevents mismatched release tags. GUI test waits and CI jobs are bounded, and portable ZIP packages receive checksums.

Validation includes x64 parser/JSON/readiness tests, isolated registry recovery, atomic installer tests, notification self-test, and full install/update rollback/stop/restart/repair/uninstall lifecycle tests. x64 and ARM64 are built; ARM64 is cross-built and not executed on native hardware.

Download the ZIP for your architecture, extract it, and run `TrayIconPromoter.exe` to install or update. Verify ZIP hashes with `SHA256SUMS.txt` and GitHub artifact attestations. The app remains offline and per-user; executables remain unsigned.

JSON reports include the installed path, which can contain your username. Review before sharing. See the [current README](https://github.com/byassin/tray-icon-promoter/blob/main/README.md) for a PowerShell export example and [the review](https://github.com/byassin/tray-icon-promoter/blob/main/docs/REVIEW-v1.2.0.md) for findings and regression coverage.

## Documentation — refreshed November 6, 2026

The original v1.2.0 application ZIPs and their attested `SHA256SUMS.txt` are unchanged. Their bundled README reflects the original build. Use the current online README or download `TrayIconPromoter-v1.2.0-documentation.zip` for the refreshed documentation, security policy, changelog, contributor guidance, and review record. The documentation-only archive has a separate `DOCUMENTATION-SHA256SUMS.txt`; it is not an application update and is not part of the original build attestation.

Full comparison: https://github.com/byassin/tray-icon-promoter/compare/v1.1.1...v1.2.0
