# LoRaGotchi DX 🐟📡

**LoRaGotchi DX v1.0 — M0YNW** is a virtual LoRa fish built on the MeshCore transport. Your fish grows through pet stages, lives in a small aquarium UI, and can travel over the mesh to visit another LoRaGotchi device before being recalled home.

## v1.0 highlights

- LoRaGotchi virtual-pet aquarium and evolution/gameplay
- DX fish visiting using the LFG1 protocol
- Send, acknowledge, commit, arrival confirmation and recall/return flow
- Host and visiting fish can coexist and swim independently
- Visiting fish keeps its own evolution stage
- Travel is locked while the local fish is asleep
- DX session state is cleared on reboot as a recovery mechanism
- LoRaGotchi / M0YNW boot branding
- MeshCore remains the underlying radio/mesh transport

## Supported LoRaGotchi targets in this tree

### ThinkNode M2

PlatformIO environment:

```bash
pio run -e ThinkNode_M2_LoRaFish_companion_ble
```

### Heltec WiFi LoRa 32 V3

PlatformIO environment:

```bash
pio run -e Heltec_v3_LoRaFish_companion_ble
```

### Heltec WiFi LoRa 32 V4 (OLED)

PlatformIO environment:

```bash
pio run -e heltec_v4_LoRaFish_companion_ble
```

### Heltec T096 (nRF52840, TFT)

PlatformIO environment:

```bash
pio run -e Heltec_t096_LoRaFish_companion_ble -t create_uf2
```

The T096 target uses its existing BLE companion transport, TFT display, and external GNSS support. After the build, enter the board's UF2 bootloader and copy `.pio/build/Heltec_t096_LoRaFish_companion_ble/firmware.uf2` to its mounted drive. Device-level validation is still needed.

LoRaGotchi appearance settings are in **Options**. Color TFTs offer four firmware themes and six fish colors; monochrome OLEDs offer dark and paper themes, while fish color selections are saved but cannot be shown in color on the OLED.

The project tree also retains the wider upstream MeshCore targets and infrastructure from which LoRaGotchi is derived.

## Building

Install PlatformIO, clone the repository, then build the required environment from the repository root. For example:

```bash
git clone <your-repository-url>
cd LoRaGotchi-DX
pio run -e ThinkNode_M2_LoRaFish_companion_ble
```

or:

```bash
pio run -e Heltec_v3_LoRaFish_companion_ble
```

Upload with PlatformIO using the normal `-t upload` target for supported serial-programmed boards. The T096 uses the UF2 procedure above.

## DX behaviour

A LoRaGotchi can request a visit to another compatible peer. The peer reserves a guest slot, acknowledges the request, accepts the committed visitor, and confirms arrival. Recall returns the visiting fish to its home device. Protocol messages are designed to tolerate repeated transaction messages during a session.

DX commits include the visitor's color capability and selected fish color. Color-capable tanks render colored visitors in their chosen hue and monochrome visitors in cyan; monochrome displays render visitors using the active foreground color. A fish returning from a monochrome host briefly shakes back into its saved home color.

DX travel state is intentionally **session state** in v1.0: rebooting a LoRaGotchi returns its local fish home and empties its guest slot. Normal pet/game state remains persistent.

## Project history

Development notes from the experimental LoRaFish/LoRaGotchi builds are retained under [`docs/development/`](docs/development/) for reference. The v1.0 release note is under [`docs/release/`](docs/release/).

## Credits

LoRaGotchi DX is by **Robert Rayner / M0YNW** and is based on **MeshCore** by Scott Powell / Ripple Radios. MeshCore provides the underlying mesh transport and substantial portions of the firmware foundation.

See [`NOTICE`](NOTICE) for attribution details.

## License

MIT. See [`LICENSE`](LICENSE).
