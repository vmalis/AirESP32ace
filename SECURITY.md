# Security

## Reporting

Please report a suspected vulnerability privately to the repository owner through GitHub before opening a public issue with exploitable details.

## Credential handling

- API and Wi-Fi credentials belong in local device/app configuration only.
- Never attach an `sdkconfig`, NVS image, application-support folder, or unredacted diagnostic bundle to a public issue.
- Version 0.1.5 pairs a single Mac over USB before Wi-Fi control. The companion pins the board's HTTPS certificate and uses the pairing token; pairing another Mac invalidates the old authorization. Do not publish pairing records, tokens or private keys. Treat the board as a trusted-home-network accessory and do not expose it directly to the public internet.
- Physical access to the board may permit flash extraction. Do not store credentials on it that would create unacceptable risk if copied.

This is a personal open-source project, not a certified aviation, weather, navigation, or safety system.
