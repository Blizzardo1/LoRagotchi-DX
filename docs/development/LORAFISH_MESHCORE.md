# LoRaFish MeshCore Edition v0.1

Target: ThinkNode M2.

This is based directly on the supplied MeshCore v1.17.1 source. It does not replace
MeshCore's radio or Companion BLE implementation.

## Build

```bash
pio run -e ThinkNode_M2_LoRaFish_companion_ble
```

Upload with the same environment:

```bash
pio run -e ThinkNode_M2_LoRaFish_companion_ble -t upload
```

## Behaviour

- The normal MeshCore Companion BLE interface remains active.
- The normal phone Companion app still handles messages, contacts, channels and configuration.
- The ThinkNode screen stays in the LoRaFish aquarium instead of exposing MeshCore pages.
- Incoming messages make the fish react but message text is not displayed on-device.
- MeshCore ACK events produce a transmit reaction.
- Pet state is stored in `/lorafish.dat` on SPIFFS.
- Evolution: Fry <1 h, Young 1–6 h, Adult 6 h–3 d, Elder 3 d+.
- Fish animation is non-blocking and refreshes at ~10 FPS.
- OLED auto-off remains enabled (30 s) to protect battery/display.

## Deliberately not in v0.1

- LoRaFish BLE roaming. MeshCore owns BLE in this build.
- Feeding/care menus.
- DS1307 integration.
- Fish-to-fish MeshCore protocol.
- Displaying message contents on the LoRaFish screen.

Those can be layered on after proving that the standard Companion app remains reliable.


## v0.1.1 compile fix

MeshCore's abstract `DisplayDriver` does not expose `drawLine()` or `drawCircle()`.
The LoRaFish aquarium now uses only the portable DisplayDriver API:
`fillRect()`, `drawRect()`, text and colour methods. The fish is rendered as
monochrome pixel art, so it remains compatible with the ThinkNode M2 OLED driver.


## v0.2 UI

- Restores a fuller monochrome aquarium with a larger animated fish.
- ENTER cycles: Aquarium -> Stats -> Clock -> Aquarium.
- Stats page: age, evolution stage, happiness, RF events, messages, ACKs, BLE state.
- Clock page: HH:MM and firmware date. It is explicitly labelled as a build-time
  clock for now; DS1307 support is the planned accurate-time source.
- Pairing PIN `123456` is shown prominently for the first 20 seconds after boot
  while the phone is disconnected. MeshCore's stock UI still remains hidden.
- Companion BLE/radio implementation is unchanged.


## v0.2.1 compile fix

MeshCore's abstract `DisplayDriver` supports `print()` but not `printf()`.
Stats strings are now formatted with `snprintf()` into a local buffer and passed
to `display.print()`. No MeshCore radio/BLE behaviour was changed.
