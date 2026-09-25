# LoRaFish MeshCore Edition v0.3.3 — RF Monitor

Adds a sixth LoRaFish menu page: RF MONITOR.

The page reads real RX metrics from MeshCore's `MyMesh::logRxRaw()` path rather than
taking control of the radio. It displays:
- latest RSSI
- latest SNR
- 48-sample RSSI history graph
- time since last RF receive
- battery percentage
- graphical battery gauge

The existing Aquarium, Stats, Clock, Phone Pairing and Options pages are unchanged.
Short-press navigation now cycles through six pages.
