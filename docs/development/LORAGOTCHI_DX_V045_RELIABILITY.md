LoRagotchi DX v0.4.5 reliability build
- M2 persists fish AWAY transaction in ESP32 Preferences.
- RETURN/BUSY/ACK transitions save immediately.
- A reboot while AWAY retains the away state and destination.
- A reboot during REQUESTING safely restores the fish home rather than assuming delivery.
- Initial REQ times out after 20 seconds and can be retried.
- CardPuter persists reserved/present DX guest state.
- Duplicate REQ with the same transaction is idempotent and receives ACK again.
- Occupied CardPuter returns BUSY to a different fish.
