# v0.3.4 RTC compile fix

The first v0.3.4 patch incorrectly assumed `rtc_clock` was a member of `UITask`.
It is not. MeshCore already passes the real `mesh::RTCClock*` into `HomeScreen`.

This revision:
- removes `UITask::getMeshTime()`
- stores the real RTC pointer supplied to `HomeScreen`
- LoRaFish reads `loRaFishRTC->getCurrentTime()`
- retains build-time fallback until the MeshCore epoch is valid
