# Security policy

## Supported versions

Security fixes are applied to the latest release line.

| Version | Supported |
| --- | --- |
| 1.1.x | Yes |
| 1.0.x and earlier | No |

## Reporting a vulnerability

Please do not open a public issue for a vulnerability that could put users at risk. Use GitHub's private [Report a vulnerability](https://github.com/byassin/tray-icon-promoter/security/advisories/new) form.

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

Dependabot monitors GitHub Actions dependencies, and secret scanning with push protection is enabled for the repository.

## Release integrity

Release ZIP packages are built by GitHub Actions and accompanied by `SHA256SUMS.txt` and a GitHub artifact attestation. Verify a downloaded package with:

```powershell
gh attestation verify .\TrayIconPromoter-vX.Y.Z-x64.zip `
  --repo byassin/tray-icon-promoter `
  --signer-workflow byassin/tray-icon-promoter/.github/workflows/release.yml `
  --deny-self-hosted-runners
```

Artifact attestations identify the GitHub workflow and source revision that produced a package. Release executables are not currently Authenticode-signed, so an attestation does not prevent a Microsoft Defender SmartScreen reputation warning.
