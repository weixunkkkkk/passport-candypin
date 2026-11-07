[简体中文](CHANGELOG.zh_CN.md)

# Changelog

## Unreleased

## v0.5.5 - 2026-11-07

This is an experimental prerelease for the ESP32-C3 AI Passport with 8 MB Flash.

- Add a screen-off policy under screen and sleep settings. The default power-saving policy stops Wi-Fi, mDNS and MCP sessions when the display sleeps. Continuous sampling remains available.
- Enter deep standby after 60 seconds with the screen off, once the network has stopped, the keys are released and no USB host is connected. A physical key wakes and restarts the application with saved configuration and album data. Consume the wake gesture to avoid starting a game immediately.
- Enable dynamic frequency scaling and automatic light sleep. Suspend the idle audio codec and I2S. Keep USB debugging and flashing available while a USB host is connected.
- Migrate the previous six-byte display settings, retaining style, brightness, screen timeout and sampling intervals when NVS is preserved. Default the new policy to power saving.

Build, host tests, partition checks and merged-image validation passed. Existing device records confirm v0.5.5 startup, settings migration, network and sampling suspension with USB connected, and resumed five-port LAN readings after waking the display through a serial key command.

Unverified on hardware: physical-key wake after unplugged deep standby, standby current, overnight battery life, phone captive-portal behavior, discovery after fresh provisioning, and live MCP readings. No battery-life duration is claimed.

Install the merged `candypin-full.bin` from address `0x0` with the official web flasher. A merged flash may reset Wi-Fi configuration and album data. A compatible application-only update can preserve NVS. See the [installation and build instructions](../README.md).
