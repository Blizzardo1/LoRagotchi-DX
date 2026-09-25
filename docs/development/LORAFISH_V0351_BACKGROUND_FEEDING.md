# LoRaFish v0.3.5.1 — Background Feeding Logic Fix

Feeding gameplay is no longer coupled to display rendering.

- Every 10 RF events credits a meal immediately, even while the OLED is off.
- Happiness is updated at meal time.
- The poop timer is scheduled at meal time.
- The next-feed threshold advances immediately.
- If the aquarium is awake when the meal is earned, the food-drop/eating animation plays.
- If the screen is asleep or another page is open, the meal still happens silently in the background.
- Waking the screen later does not cause stale food to suddenly drop.
- The visible feeding animation no longer applies happiness or schedules poop, preventing double credit.
- Multiple feed thresholds crossed while asleep are consumed correctly.
