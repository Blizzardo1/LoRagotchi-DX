# LoRaFish v0.3.5.4 — Sleep Behaviour Fix

Two different kinds of sleep are now handled correctly.

## OLED off
The pet is still awake. RF/gameplay continues, food is credited normally, and the food-eaten
beep still sounds. Incoming MeshCore messages do NOT turn the OLED back on.

## Scheduled fish sleep (21:00–07:00 local time)
The fish is actually asleep. MeshCore/RF activity is still counted, but automatic food is not
generated/eaten, there is no eating beep, and no poop is scheduled from sleeping-time traffic.
Feed thresholds are consumed while asleep so food does not accumulate and burst on screen at 07:00.

The OLED is now only woken by user input, not by each received MeshCore message.
