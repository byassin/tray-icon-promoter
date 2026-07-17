# Security policy

## Supported versions

Security fixes are applied to the latest release.

## Reporting a vulnerability

Please do not open a public issue for a vulnerability that could put users at risk. Use GitHub's **Report a vulnerability** feature under the repository's Security tab. If private vulnerability reporting is not enabled, contact the maintainer through the email address on their GitHub profile.

Include:

- the affected version and architecture;
- the relevant Windows version;
- reproduction steps;
- expected and observed behavior;
- any proof of concept or crash information.

Please allow a reasonable period for investigation before public disclosure.

## Security properties

Tray Icon Promoter is designed to:

- run without administrator privileges;
- operate only in the current user's registry and local application-data directory;
- avoid process injection, Explorer hooks, drivers, services, and scheduled tasks;
- make no network connections;
- reject duplicate watcher instances.

Release binaries are built by GitHub Actions and accompanied by SHA-256 checksums. They are not currently Authenticode-signed.

