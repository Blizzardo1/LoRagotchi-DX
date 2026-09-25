# LoRaFish — MeshCore node-name sync

The aquarium/pet header now uses the existing MeshCore `NodePrefs::node_name` instead of
the hardcoded `LoRaFish` title.

- Changing the companion name updates the same persisted `node_name` buffer.
- LoRaFish holds a pointer to that buffer, so no duplicate name setting is required.
- Empty/unset names fall back to `LoRaFish`.
- The display name is limited to 15 characters to avoid colliding with the clock/status
  on the 128-pixel-wide ThinkNode M2 display.
- Egg, aquarium, sleeping and memorial pet screens all use the configured name.
