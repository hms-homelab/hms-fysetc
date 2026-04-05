# Changelog

All notable changes to this project will be documented in this file.

Version format: `YYYY.MINOR.PATCH` where YYYY is the release year, MINOR is the feature increment, and PATCH is the bug fix increment.

## [2026.1.0] - 2026-04-05

### Added
- Captive portal WiFi setup with DNS hijack (AP: `FYSETC-Setup-XXXX`)
- NVS credential storage — WiFi credentials persist across reboots
- Runtime WiFi via `wifi_manager_connect_dynamic()`
- Boot flow: NVS credentials -> Kconfig defaults -> captive portal
- Auto-clear NVS and reboot into portal on connection failure
- WiFi network scan dropdown in setup form

### Changed
- WiFi no longer requires compile-time configuration — can be set at runtime

## [2026.0.0] - 2026-04-05

### Added
- HTTP file server for FYSETC SD WiFi Pro
- `GET /dir` — HTML directory listing
- `GET /download` — raw file download with Range header support
- `GET /api/status` — JSON device status
- `GET /` — web dashboard with live logs and PCNT bus activity chart
- SD bus MUX control — read-only, released between requests
- PCNT hardware pulse counter for passive bus monitoring
- Compile-time WiFi configuration via `idf.py menuconfig`
