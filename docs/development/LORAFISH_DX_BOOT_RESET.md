# LoRaFish DX boot-reset baseline

DX travel state is now session-only. Every reboot returns the local fish HOME and clears the guest/reservation slot. Pet identity and normal Tamagotchi state remain persistent. The stable DX fish ID is retained, but pending/committing/away/returned, destination, guest presence/reservation, transaction IDs and retry state do not survive reboot.
