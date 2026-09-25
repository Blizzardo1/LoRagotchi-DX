# LoRaFish DX statefix2

Fixes three peer-transfer bugs:

1. `LFG1|ARRIVED|` is 13 characters including the trailing separator. The sender previously compared/parses at 12, so a valid ARRIVED was logged but never accepted and `away` was never set.
2. Guest rendering now uses `loragotchiDXGuest.stage` instead of the local fish evolution, so a visiting FRY remains a FRY.
3. Recall is permitted while COMMITTING as well as AWAY, providing recovery if final arrival confirmation is delayed/lost.
