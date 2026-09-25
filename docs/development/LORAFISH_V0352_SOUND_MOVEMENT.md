# LoRaFish v0.3.5.2 — Sound + Feeding Movement Fix

- Food-earned beep now plays from `UITask::loop()`, so it sounds even with OLED off.
- Background meal processing is genuinely independent of rendering.
- Cleaning completion plays a short ascending melody.
- Feeding animation captures the fish's actual current swim position.
- Fish swims from that position toward food; no teleport to x=22.
- Direction is derived from the food's position, so a fish near the right edge can turn and swim left naturally.
- Hygiene timing also runs in the main UI loop rather than depending on aquarium rendering.
