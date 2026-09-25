# LoRaFish MeshCore Edition v0.2.2

UI/pairing cleanup based on hardware testing.

Changes:
- Dedicated fourth `PHONE PAIRING` page; it no longer hijacks the aquarium at boot.
- Pairing page reads `the_mesh.getBLEPin()` — the real MeshCore BLE PIN.
- LoRaFish builds now keep `BLE_PIN_CODE` stable across reboots instead of generating a
  new random PIN every boot. This avoids stale iOS BLE bonds causing repeated
  `Encryption is insufficient` failures.
- Aquarium/header separator lines moved clear of text.
- Fish sprite redrawn with a tapered body, tail, fins and eye instead of a solid block.
- ENTER cycles Aquarium -> Stats -> Clock -> Phone Pairing.

After flashing v0.2.2, an iPhone that previously bonded to an older random-PIN build may
need the old Bluetooth bond removed once. Pair again using the PIN shown on the dedicated
PHONE PAIRING page. With the stable PIN, subsequent reboots should not create a new bond mismatch.
