# LoRaFish DX statefix4

- Blocks new DX travel while the local fish is asleep (recall remains available).
- Guest fish uses the same smooth, stage-aware swimming path as the local fish.
- Guest movement is phase shifted so two fish swim independently without freezing the host.
- Guest movement is constrained to the aquarium area rather than sweeping across the UI.
- A sleeping local fish remains asleep while an already-present guest can continue swimming.
- Guest stage rendering remains independent from the host fish evolution.
