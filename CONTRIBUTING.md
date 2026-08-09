# Contributing

Thanks for helping improve AirESP32ace.

## Before opening a change

1. Keep changes focused and describe the hardware or macOS version used for testing.
2. Preserve smooth partial-update rendering; avoid introducing full-screen redraws into animation paths.
3. Do not commit API credentials, Wi-Fi settings, generated maps, downloaded airline logos, the proprietary watch mark, build folders, or proprietary fonts.
4. Add or update tests when changing parsing, projection, configuration, or device-protocol behavior.
5. Include a photo or screen capture for visual changes when practical.

## Verify the Mac app

```bash
cd companion/AirspaceCompanion
swift test
```

Internal screenshot/guide renderers are intentionally kept out of the default test target because they require a window-server session and the native LVGL renderer.

## Verify the firmware

```bash
./scripts/idf.sh firmware/airspace-instrument build
```

For display or input work, also test on the physical Waveshare board. A successful compile does not prove correct panel timing, memory lifetime, touch behavior, or power management.

## Pull requests

Explain the problem, the design choice, and how it was verified. Keep unrelated cleanup out of the same change.

## Maintainer review and protected `main`

`CODEOWNERS` assigns the complete repository to `@vmalis`. Contributors should work on a branch and open a pull request; direct changes to `main` are not part of the project workflow.

Repository administrators should create a GitHub branch ruleset for `main` with:

1. Require a pull request before merging.
2. Require at least one approval.
3. Require review from Code Owners.
4. Dismiss stale approvals when new commits are pushed.
5. Require all conversations to be resolved.
6. Block force pushes and branch deletion.
7. Apply the rule to repository administrators when strict maintainer review is desired.

GitHub enforces these server-side settings; repository files alone cannot prevent an administrator from bypassing them.
