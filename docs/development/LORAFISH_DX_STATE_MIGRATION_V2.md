# LoRaFish DX state migration v2

This build adds a versioned NVS marker for LoRaFish DX state.

On the first boot after upgrading from an unversioned/older build it:
- returns the local fish to HOME;
- clears pending/committing/away transaction state;
- clears stale guest reservations/visitors;
- preserves the local fish ID when the old home record is structurally readable;
- writes the new v2 state marker so subsequent normal reboots do not repeat the migration.

On later boots, only incomplete REQUESTING/COMMITTING sessions are cancelled. A genuinely confirmed AWAY state can persist across reboot.
