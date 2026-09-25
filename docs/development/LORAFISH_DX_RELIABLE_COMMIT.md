# LoRaFish DX reliable COMMIT

Adds an explicit ARRIVED confirmation to LFG1 peer transfers.

Flow: REQ -> ACK -> COMMIT -> ARRIVED. The home fish is only marked away after ARRIVED.
Matching duplicate COMMIT packets are idempotent and cause ARRIVED to be re-sent.
The sender retries COMMIT every 3 seconds up to four times; if no ARRIVED is received, the fish remains home.
