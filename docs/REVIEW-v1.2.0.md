# v1.2.0 review and enhancements

Reviewed the native watcher and registry access, startup/readiness lifecycle, installer and uninstall paths, command parsing and diagnostics, build scripts, CI/release workflows, and user documentation against v1.1.1.

| Finding | Change | Regression evidence |
| --- | --- | --- |
| Registry enumeration continued forever on persistent errors | Stop the scan, retain the error, let the watcher reopen the tree | Deleted-key scan is bounded; isolated tree deletion/recreation recovers |
| Readiness stayed set after registry access failed | Clear readiness on monitoring/scan failure and restore it after recovery | Recovery test observes both readiness transitions |
| Permission changes were not watched | Include registry security notifications | Notification filter includes security changes |
| A retained signaled stop event could abort a new watcher | Reset stop state after acquiring the singleton | Test starts with a retained signaled event |
| Update failure could leave the prior watcher stopped | Stage replacement, snapshot file/startup state, roll back and restart | File preservation tests and failed-update lifecycle checks |
| Lifecycle commands could interfere with each other | Serialize install, uninstall, stop, restart, and self-test | Operation-lock timeout leaves installed state intact |
| Uninstall could report success after file deletion failed | Handle read-only files and report removal errors; allow repeated uninstall | Read-only and repeated-uninstall checks |
| Status hid partial scan errors and reported only the caller version | Preserve counts; report installed version, readiness, and recommendation | JSON formatting and lifecycle assertions |
| Users lacked a temporary stop/restart control | Add `--stop` and `--restart`, preserving startup preferences | Idempotent stop and disabled-startup-preserving restart |
| No structured diagnostic export | Add `--status-json` through stdout | JSON round-trip with Unicode/spaced paths and escaped strings |
| Local ZIP was missing from its checksum manifest | Generate checksums after packaging | Manifest covers executable and ZIP |
| A mistag could publish a mismatched executable | Validate all version metadata and exact release tag | Positive validation and mismatched-tag rejection |
| CI could wait indefinitely for a GUI process tree | Bound self-test waits and workflow jobs | Explicit process and job timeouts |
| Test setup could overwrite empty/disabled startup values | Refuse any existing registration | Existence-based preflight checks |

The app remains an offline, per-user, event-driven utility. The review does not add network updates, services, elevation, Explorer injection, or per-icon rules.

ARM64 binaries and test targets are cross-built; runtime tests execute on x64 Windows. Authenticode signing and native ARM64 runtime validation remain outside this release. Windows registry layouts are implementation details, so future Windows builds still require compatibility verification.
