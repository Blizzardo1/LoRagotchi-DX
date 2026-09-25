# LoRaFish MeshCore Edition v0.3.4 — MeshCore Time Sync

LoRaFish now reads time from MeshCore's existing `rtc_clock` through `UITask::getMeshTime()`.

- Clock page uses MeshCore epoch time when it is valid.
- Aquarium/header clock uses the same source.
- 21:00–07:00 fish sleep schedule uses the same source automatically.
- No Wi-Fi or NTP stack is added.
- If MeshCore RTC is not yet valid, LoRaFish falls back to firmware build time + uptime.
- The clock page shows `MeshCore RTC (UTC)` when synced and `build-time fallback` otherwise.

Note: MeshCore RTC is an epoch clock. This build displays it as UTC. A local-time offset/timezone
setting can be added separately without changing the sync source.
