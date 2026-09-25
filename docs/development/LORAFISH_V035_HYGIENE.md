# LoRaFish v0.3.5 — Tank Hygiene

Adds a Tamagotchi-style poop/cleaning mechanic.

- After a completed meal, one poop is scheduled 30–90 seconds later.
- Up to three poops can accumulate.
- Each poop has a tiny two-frame wobble animation on the aquarium floor.
- Long-press on the Aquarium starts a 1.4-second broom sweep.
- Poop disappears behind the broom as it crosses the tank.
- Cleaning clears all waste and gives +3 happiness.
- Poop count is stored in `/lorafish.cfg` alongside screen timeout and survives reboot.
- Old one-byte config files remain compatible.
- Resetting the pet also resets tank hygiene.
- Options-page long presses retain their existing timeout/reset/RF-monitor actions.
- Includes the confirmed UK GMT/BST local-time conversion.
