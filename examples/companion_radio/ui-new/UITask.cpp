#include "UITask.h"
#include "../MyMesh.h"
#ifdef ESP32
#include <SPIFFS.h>
#endif
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#ifdef WIFI_SSID
  #include <WiFi.h>
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"


static mesh::RTCClock* loRaFishRTC = nullptr;

static const char* loRaFishMeshNodeName = nullptr;

static const char* loRaFishDisplayName() {
  if (!loRaFishMeshNodeName || !loRaFishMeshNodeName[0]) return "LoRaFish";

  // Keep the left header clear of the clock/right-side status on 128px displays.
  static char shortName[16];
  strncpy(shortName, loRaFishMeshNodeName, sizeof(shortName)-1);
  shortName[sizeof(shortName)-1] = 0;
  return shortName;
}

// ============================================================
// LoRaFish MeshCore Edition v0.3.2 - DisplayDriver compatibility
// MeshCore owns radio + Companion BLE. This pet layer only observes UI events.
// ============================================================
struct LoRaFishState {
  uint32_t magic;
  uint32_t ageMinutes;
  uint32_t rfEvents;
  uint32_t messages;
  uint32_t txAcks;
  uint8_t happiness;
  uint8_t evolution;
};

static constexpr uint32_t LORAFISH_MAGIC = 0x4C464D31; // "LFM1"
static void saveLoRaFish();
static void saveLoRaFishSettings();
static void scheduleLoRaFishPoop();
static LoRaFishState fish = {LORAFISH_MAGIC, 0, 0, 0, 0, 80, 0};
static unsigned long fishLastMinute = 0;
static unsigned long fishLastSave = 0;
static unsigned long fishReactionUntil = 0;
static uint8_t fishReaction = 0; // 0 normal, 1 RX, 2 TX/ACK, 3 new contact

static uint8_t loRaFishPage = 0; // 0 aquarium, 1 stats, 2 clock, 3 phone pairing, 4 DX visit, 5 options, 6 RF monitor
static uint8_t loRaFishOption = 0; // 0 screen timeout, 1 reset pet
static constexpr uint8_t LORAFISH_PAGE_COUNT = 7;
static int8_t loRaFishRfHistory[48] = {0};
static uint8_t loRaFishRfHistoryCount = 0;
static uint32_t loRaFishRfSeenSeq = 0;

// Lifecycle presentation: egg for first 5 minutes, then normal fish stages.
// At 7 days the pet reaches the end of its life and shows a memorial screen.
static constexpr uint32_t LORAFISH_EGG_MINUTES = 5;
static constexpr uint32_t LORAFISH_LIFESPAN_MINUTES = 7UL*24UL*60UL;
static bool loRaFishHatchMelodyPlayed = false;
static unsigned long loRaFishEvolutionFlashUntil = 0;

// User-selectable screen timeout. 0=15s, 1=30s, 2=60s, 3=always on.
static uint8_t loRaFishTimeoutSetting = 0;
static const uint32_t loRaFishTimeoutValues[] = {15000UL,30000UL,60000UL,0UL};
static uint32_t loRaFishScreenTimeoutMs() {
  return loRaFishTimeoutValues[min((uint8_t)3,loRaFishTimeoutSetting)];
}
static unsigned long loRaFishBootMs = 0;

#define LORAFISH_TIMEOUT_MS() loRaFishScreenTimeoutMs()
// Tamagotchi-style automatic feeding and sleep cycle.
static bool loRaFishFeeding = false;
static bool loRaFishFeedPipped = false;
static unsigned long loRaFishFeedStarted = 0;
static int loRaFishFeedStartX = 22;
static int loRaFishFeedStartY = 35;
static bool loRaFishFeedStartRight = true;
static constexpr unsigned long LORAFISH_FEED_DROP_MS = 1300UL;
static constexpr unsigned long LORAFISH_FEED_DONE_MS = 1750UL;
static constexpr uint32_t LORAFISH_PACKETS_PER_FEED = 10;

static void loRaFishNaturalPositionForStage(int w, uint8_t stage, unsigned long phaseOffset,
                                             int& x, int& y, bool& right) {
  // Keep every fish inside the actual aquarium area.  Visitors use the same
  // smooth triangular swim path as the home fish, just phase-shifted so two
  // fish do not sit on top of each other.
  unsigned long swimPeriod=8000UL;
  if(stage==0) swimPeriod=5200UL;
  else if(stage==1) swimPeriod=6500UL;
  else if(stage>=3) swimPeriod=11000UL;

  unsigned long t=(millis()+phaseOffset)%swimPeriod;
  float phase=t/(float)swimPeriod;
  right=phase<0.5f;

  const int margin=20;                 // sprite-safe left/right tank margin
  const int span=max(1,w-(margin*2));
  x=right ? margin+(int)(span*phase*2.0f)
          : margin+(int)(span*(1.0f-phase)*2.0f);
  y=38+(int)(2.0f*sinf((millis()+phaseOffset)/650.0f));
}

static void loRaFishNaturalPosition(int w, int& x, int& y, bool& right) {
  loRaFishNaturalPositionForStage(w,fish.evolution,0,x,y,right);
}

// Gameplay and presentation are deliberately separate:
// a meal is credited immediately when RF earns it; this animation is optional.
static void creditLoRaFishMeal(bool animate, int displayWidth) {
  fish.happiness = (fish.happiness <= 97) ? fish.happiness + 3 : 100;
  scheduleLoRaFishPoop();
  saveLoRaFish();

  if (animate && !loRaFishFeeding) {
    loRaFishNaturalPosition(displayWidth, loRaFishFeedStartX, loRaFishFeedStartY, loRaFishFeedStartRight);
    loRaFishFeeding = true;
    loRaFishFeedPipped = false;
    loRaFishFeedStarted = millis();
  }
}
static uint32_t loRaFishNextAutoFeed = LORAFISH_PACKETS_PER_FEED;
static constexpr uint8_t LORAFISH_SLEEP_HOUR = 21;
static constexpr uint8_t LORAFISH_WAKE_HOUR = 7;

// Tamagotchi-style tank hygiene.
static uint8_t loRaFishPoopCount = 0;
static unsigned long loRaFishNextPoopAt = 0;
static bool loRaFishCleaning = false;
static unsigned long loRaFishCleaningStarted = 0;
static constexpr uint8_t LORAFISH_MAX_POOP = 3;
static constexpr unsigned long LORAFISH_CLEAN_MS = 1400UL;

static void scheduleLoRaFishPoop() {
  // 30-90 seconds after a meal. A pending poop is not rescheduled by another meal.
  if (loRaFishNextPoopAt == 0 && loRaFishPoopCount < LORAFISH_MAX_POOP)
    loRaFishNextPoopAt = millis() + 30000UL + (esp_random() % 60001UL);
}

static bool tickLoRaFishPoop() {
  bool cleaned = false;
  if (loRaFishNextPoopAt && (long)(millis() - loRaFishNextPoopAt) >= 0) {
    if (loRaFishPoopCount < LORAFISH_MAX_POOP) loRaFishPoopCount++;
    loRaFishNextPoopAt = 0;
    saveLoRaFishSettings();
  }
  if (loRaFishCleaning && millis() - loRaFishCleaningStarted >= LORAFISH_CLEAN_MS) {
    loRaFishCleaning = false;
    loRaFishPoopCount = 0;
    loRaFishNextPoopAt = 0;
    fish.happiness = min((int)100, (int)fish.happiness + 3);
    saveLoRaFishSettings();
    saveLoRaFish();
    cleaned = true;
  }
  return cleaned;
}

static void startLoRaFishCleaning() {
  if (!loRaFishCleaning && loRaFishPoopCount > 0) {
    loRaFishCleaning = true;
    loRaFishCleaningStarted = millis();
  }
}

static void startLoRaFishFeeding() {
  // Presentation only. Meal state is credited by creditLoRaFishMeal().
  if (!loRaFishFeeding) {
    loRaFishFeeding = true;
    loRaFishFeedPipped = false;
    loRaFishFeedStarted = millis();
  }
}

static void resetLoRaFishPet() {
  // Recreate the initial persisted pet state.
  fish = {LORAFISH_MAGIC, 0, 0, 0, 0, 80, 0};
  fishReaction = 0;
  fishReactionUntil = 0;
  loRaFishFeeding = false;
  loRaFishFeedPipped = false;
  loRaFishFeedStarted = 0;
  loRaFishEvolutionFlashUntil = 0;
  loRaFishNextAutoFeed = LORAFISH_PACKETS_PER_FEED;
  loRaFishPoopCount = 0;
  loRaFishNextPoopAt = 0;
  loRaFishCleaning = false;
  saveLoRaFishSettings();
  fishLastMinute = millis();
  fishLastSave = millis();
  saveLoRaFish();
}

// LoRaFish uses MeshCore's RTC as its source of truth. If MeshCore has not
// acquired a plausible epoch yet, retain the old build-time + uptime fallback.
static uint32_t buildSecondsOfDay() {
  int hh=0, mm=0, ss=0;
  sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
  return (uint32_t)hh*3600UL + (uint32_t)mm*60UL + (uint32_t)ss;
}

static bool loRaFishMeshTimeValid(uint32_t epoch) {
  // 2024-01-01 UTC. Avoid treating an unset/near-zero RTC as real time.
  return epoch >= 1704067200UL;
}

static bool loRaFishClock(char* timeBuf, size_t timeLen,
                          char* dateBuf, size_t dateLen) {
  uint32_t epoch = loRaFishRTC ? loRaFishRTC->getCurrentTime() : 0;

  if (loRaFishMeshTimeValid(epoch)) {
    time_t raw = (time_t)epoch;
    struct tm tmv;
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0/2", 1);
    tzset();
    localtime_r(&raw, &tmv);
    snprintf(timeBuf, timeLen, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    static const char* mon[]={"Jan","Feb","Mar","Apr","May","Jun",
                              "Jul","Aug","Sep","Oct","Nov","Dec"};
    snprintf(dateBuf, dateLen, "%02d %s %04d",
             tmv.tm_mday, mon[tmv.tm_mon], tmv.tm_year+1900);
    return true;
  }

  uint32_t sec = buildSecondsOfDay() + millis()/1000UL;
  uint32_t days = sec / 86400UL;
  sec %= 86400UL;
  snprintf(timeBuf, timeLen, "%02lu:%02lu",
           (unsigned long)(sec/3600UL),
           (unsigned long)((sec/60UL)%60UL));
  if (days == 0) snprintf(dateBuf, dateLen, "%s", __DATE__);
  else snprintf(dateBuf, dateLen, "%s +%lud", __DATE__, (unsigned long)days);
  return false;
}

static uint8_t fishEvolutionForAge(uint32_t mins) {
  // evolution still describes the fish after hatching; egg is age-driven.
  if (mins < 60) return 0;             // Fry
  if (mins < 6UL*60UL) return 1;       // Young
  if (mins < 3UL*24UL*60UL) return 2;  // Adult
  return 3;                            // Elder
}

static const char* fishStageName() {
  switch (fish.evolution) {
    case 0: return "FRY";
    case 1: return "YOUNG";
    case 2: return "ADULT";
    default: return "ELDER";
  }
}

static void saveLoRaFishSettings() {
#ifdef ESP32
  File f = SPIFFS.open("/lorafish.cfg", FILE_WRITE);
  if (f) {
    f.write(&loRaFishTimeoutSetting, 1);
    f.write(&loRaFishPoopCount, 1);
    f.close();
  }
#endif
}

static void loadLoRaFishSettings() {
#ifdef ESP32
  File f = SPIFFS.open("/lorafish.cfg", FILE_READ);
  if (f) {
    uint8_t v=0;
    if (f.read(&v,1)==1 && v<=3) loRaFishTimeoutSetting=v;
    uint8_t poop=0;
    if (f.read(&poop,1)==1 && poop<=LORAFISH_MAX_POOP) loRaFishPoopCount=poop;
    f.close();
  }
#endif
}

static void saveLoRaFish() {
#ifdef ESP32
  File f = SPIFFS.open("/lorafish.dat", FILE_WRITE);
  if (f) {
    f.write((const uint8_t*)&fish, sizeof(fish));
    f.close();
  }
#endif
  fishLastSave = millis();
}

static void loadLoRaFish() {
#ifdef ESP32
  File f = SPIFFS.open("/lorafish.dat", FILE_READ);
  if (f && f.size() == sizeof(fish)) {
    LoRaFishState tmp;
    if (f.read((uint8_t*)&tmp, sizeof(tmp)) == sizeof(tmp) &&
        tmp.magic == LORAFISH_MAGIC) fish = tmp;
    f.close();
  }
#endif
  fish.evolution = fishEvolutionForAge(fish.ageMinutes);
  fishLastMinute = millis();
  fishLastSave = millis();
  loRaFishNextAutoFeed =
      ((fish.rfEvents / LORAFISH_PACKETS_PER_FEED) + 1) * LORAFISH_PACKETS_PER_FEED;
}

static void tickLoRaFish() {
  unsigned long now = millis();
  while (now - fishLastMinute >= 60000UL) {
    fishLastMinute += 60000UL;
    fish.ageMinutes++;
    uint8_t previousEvolution=fish.evolution;
    fish.evolution = fishEvolutionForAge(fish.ageMinutes);
    if (fish.ageMinutes >= LORAFISH_EGG_MINUTES && fish.evolution != previousEvolution)
      loRaFishEvolutionFlashUntil=millis()+5000UL;
    if (fish.ageMinutes == LORAFISH_EGG_MINUTES) {
      loRaFishHatchMelodyPlayed = false; // UITask::loop will play it once.
    }
    if ((fish.ageMinutes % 5) == 0) saveLoRaFish();
  }
  if (now - fishLastSave >= 15UL*60UL*1000UL) saveLoRaFish();
}

static void reactLoRaFish(uint8_t reaction) {
  fishReaction = reaction;
  fishReactionUntil = millis() + 2500UL;
  fish.rfEvents++;
  if (fish.happiness < 100) fish.happiness++;
}

// -----------------------------------------------------------------------------
// LoRaFish drawing compatibility helpers
//
// MeshCore's DisplayDriver intentionally exposes a smaller primitive API than
// the U8g2-style driver used by the standalone/roaming LoRaFish firmware.
// Build pixels and arbitrary lines from fillRect(), which DisplayDriver supports.
// -----------------------------------------------------------------------------
static inline void fishPixel(DisplayDriver& display, int x, int y) {
  display.fillRect(x, y, 1, 1);
}

static void fishLine(DisplayDriver& display, int x0, int y0, int x1, int y1) {
  // Integer Bresenham line. No heap allocation and fine for this tiny sprite.
  int dx = abs(x1 - x0);
  int sx = (x0 < x1) ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;

  while (true) {
    fishPixel(display, x0, y0);
    if (x0 == x1 && y0 == y1) break;

    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// helper

static void maskAdultFishSprite(DisplayDriver& d, int x, int y, bool right) {
  d.setColor(UIColor::popup_bkg);

  if (right) {
    // Fill the rounded body interior.
    for (int yy = -5; yy <= 5; yy++) {
      int inset = 0;

      if (yy == -5 || yy == 5) inset = 3;
      else if (yy == -4 || yy == 4) inset = 1;

      fishLine(d,
               x - 7 + inset, y + yy,
               x + 9 - inset, y + yy);
    }

    // Fill forked tail.
    for (int yy = -6; yy <= 6; yy++) {
      int ax = abs(yy);
      int left = x - 14;
      int rightEdge = x - 8 - (ax / 3);
      fishLine(d, left, y + yy, rightEdge, y + yy);
    }

    // Dorsal fin.
    for (int yy = -9; yy <= -6; yy++) {
      int half = yy + 9;
      fishLine(d,
               x - 1 - half,
               y + yy,
               x - 1 + half,
               y + yy);
    }

    // Lower fin.
    for (int yy = 6; yy <= 9; yy++) {
      int half = 9 - yy;
      fishLine(d,
               x - 1 - half,
               y + yy,
               x + 2 + half,
               y + yy);
    }

  } else {
    // Mirrored body.
    for (int yy = -5; yy <= 5; yy++) {
      int inset = 0;

      if (yy == -5 || yy == 5) inset = 3;
      else if (yy == -4 || yy == 4) inset = 1;

      fishLine(d,
               x - 9 + inset, y + yy,
               x + 7 - inset, y + yy);
    }

    // Mirrored tail.
    for (int yy = -6; yy <= 6; yy++) {
      int ax = abs(yy);
      int leftEdge = x + 8 + (ax / 3);
      fishLine(d,
               leftEdge, y + yy,
               x + 14, y + yy);
    }

    // Dorsal fin.
    for (int yy = -9; yy <= -6; yy++) {
      int half = yy + 9;
      fishLine(d,
               x + 1 - half,
               y + yy,
               x + 1 + half,
               y + yy);
    }

    // Lower fin.
    for (int yy = 6; yy <= 9; yy++) {
      int half = 9 - yy;
      fishLine(d,
               x - 2 - half,
               y + yy,
               x + 1 + half,
               y + yy);
    }
  }
}

static void drawAdultFishSprite(DisplayDriver& display, int x, int y, bool right) {
  // V0.3.1: recognisable aquarium fish designed for the real monochrome OLED.
  // Approx. 29x17 pixels. Mostly outline/detail rather than one solid blob.
  display.setColor(UIColor::primary_txt);

    // Clear everything behind the actual fish silhouette first.
  maskAdultFishSprite(display, x, y, right);

  // Now draw the fish normally.
  display.setColor(UIColor::primary_txt);

  if (right) {
    // Forked tail.
    fishLine(display, x-8, y,   x-14, y-6);
    fishLine(display, x-14,y-6, x-12, y);
    fishLine(display, x-12,y,   x-14, y+6);
    fishLine(display, x-14,y+6, x-8,  y+2);

    // Rounded body outline.
    fishLine(display, x-8,y-2, x-5,y-5);
    fishLine(display, x-5,y-5, x+3,y-6);
    fishLine(display, x+3,y-6, x+8,y-4);
    fishLine(display, x+8,y-4, x+11,y-1);
    fishLine(display, x+11,y-1,x+11,y+2);
    fishLine(display, x+11,y+2,x+7,y+5);
    fishLine(display, x+7,y+5, x-3,y+6);
    fishLine(display, x-3,y+6, x-8,y+2);

    // Head/gill gives the fish a face rather than a rectangle.
    fishLine(display, x+5,y-4, x+5,y+4);
    display.fillRect(x+7,y-1,2,2);       // eye

    // Scales / body texture.
    fishPixel(display, x-4,y-2);
    fishPixel(display, x-1,y-3);
    fishPixel(display, x+2,y-2);
    fishPixel(display, x-5,y+1);
    fishPixel(display, x-2,y+2);
    fishPixel(display, x+1,y+1);
    fishPixel(display, x+3,y+3);

    // Dorsal fin and lower fin.
    fishLine(display, x-4,y-5, x-1,y-9);
    fishLine(display, x-1,y-9, x+3,y-6);
    fishLine(display, x-1,y+6, x+2,y+9);
    fishLine(display, x+2,y+9, x+4,y+5);

    // Small mouth.
    fishPixel(display, x+12,y+1);

  } else {
    // Mirrored left-facing fish.
    fishLine(display, x+8, y,   x+14, y-6);
    fishLine(display, x+14,y-6, x+12, y);
    fishLine(display, x+12,y,   x+14, y+6);
    fishLine(display, x+14,y+6, x+8,  y+2);

    fishLine(display, x+8,y-2, x+5,y-5);
    fishLine(display, x+5,y-5, x-3,y-6);
    fishLine(display, x-3,y-6, x-8,y-4);
    fishLine(display, x-8,y-4, x-11,y-1);
    fishLine(display, x-11,y-1,x-11,y+2);
    fishLine(display, x-11,y+2,x-7,y+5);
    fishLine(display, x-7,y+5, x+3,y+6);
    fishLine(display, x+3,y+6, x+8,y+2);

    fishLine(display, x-5,y-4, x-5,y+4);
    display.fillRect(x-9,y-1,2,2);

    fishPixel(display, x+4,y-2);
    fishPixel(display, x+1,y-3);
    fishPixel(display, x-2,y-2);
    fishPixel(display, x+5,y+1);
    fishPixel(display, x+2,y+2);
    fishPixel(display, x-1,y+1);
    fishPixel(display, x-3,y+3);

    fishLine(display, x+4,y-5, x+1,y-9);
    fishLine(display, x+1,y-9, x-3,y-6);
    fishLine(display, x+1,y+6, x-2,y+9);
    fishLine(display, x-2,y+9, x-4,y+5);

    fishPixel(display, x-12,y+1);
  }

  display.setColor(UIColor::primary_txt);
}
static void drawFryFishSprite(DisplayDriver& d, int x, int y, bool right) {
  // Opaque mask
  d.setColor(UIColor::popup_bkg);

  if (right) {
    // Tail interior
    for (int i = 0; i <= 4; i++) {
      fishLine(d, x-8, y-i, x-3, y);
      fishLine(d, x-8, y+i, x-3, y);
    }

    // Body/head
    d.fillRect(x-3, y-2, 9, 5);

  } else {
    for (int i = 0; i <= 4; i++) {
      fishLine(d, x+8, y-i, x+3, y);
      fishLine(d, x+8, y+i, x+3, y);
    }

    d.fillRect(x-6, y-2, 9, 5);
  }


  
  d.setColor(UIColor::primary_txt);

  // Tiny fry: small body with a proportionally large tail.
  if (right) {
    fishLine(d,x-3,y,   x-8,y-4);
    fishLine(d,x-3,y,   x-8,y+4);
    fishLine(d,x-8,y-4, x-8,y+4);
    d.fillRect(x-3,y-2,7,5);
    d.fillRect(x+4,y-1,2,3);
    fishPixel(d,x+2,y-1); // eye/detail
  } else {
    fishLine(d,x+3,y,   x+8,y-4);
    fishLine(d,x+3,y,   x+8,y+4);
    fishLine(d,x+8,y-4, x+8,y+4);
    d.fillRect(x-4,y-2,7,5);
    d.fillRect(x-6,y-1,2,3);
    fishPixel(d,x-3,y-1);
  }
}

static void drawYoungFishSprite(DisplayDriver& d, int x, int y, bool right) {

 // Opaque mask
  d.setColor(UIColor::popup_bkg);

  if (right) {
    // Tail
    for (int i = 0; i <= 5; i++) {
      fishLine(d, x-11, y-i, x-6, y);
      fishLine(d, x-11, y+i, x-6, y);
    }

    // Body/head
    d.fillRect(x-6, y-3, 14, 7);

    // Dorsal and lower fins
    for (int i = 0; i <= 3; i++)
      fishLine(d, x-1, y-3, x+1, y-6+i);

    for (int i = 0; i <= 2; i++)
      fishLine(d, x-1, y+3, x+1, y+5-i);

  } else {
    for (int i = 0; i <= 5; i++) {
      fishLine(d, x+11, y-i, x+6, y);
      fishLine(d, x+11, y+i, x+6, y);
    }

    d.fillRect(x-8, y-3, 14, 7);

    for (int i = 0; i <= 3; i++)
      fishLine(d, x+1, y-3, x-1, y-6+i);

    for (int i = 0; i <= 2; i++)
      fishLine(d, x+1, y+3, x-1, y+5-i);
  }



  d.setColor(UIColor::primary_txt);

  // Juvenile: longer/slimmer than fry with its first proper dorsal fin.
  if (right) {
    fishLine(d,x-6,y,    x-11,y-5);
    fishLine(d,x-6,y,    x-11,y+5);
    fishLine(d,x-11,y-5, x-11,y+5);
    d.fillRect(x-6,y-3,12,7);
    d.fillRect(x+6,y-1,2,3);
    fishLine(d,x-1,y-3, x+1,y-6);
    fishLine(d,x-1,y+3, x+1,y+5);
    fishPixel(d,x+4,y-1);
  } else {
    fishLine(d,x+6,y,    x+11,y-5);
    fishLine(d,x+6,y,    x+11,y+5);
    fishLine(d,x+11,y-5, x+11,y+5);
    d.fillRect(x-6,y-3,12,7);
    d.fillRect(x-8,y-1,2,3);
    fishLine(d,x+1,y-3, x-1,y-6);
    fishLine(d,x+1,y+3, x-1,y+5);
    fishPixel(d,x-5,y-1);
  }
}

static void drawElderFishSprite(DisplayDriver& d, int x, int y, bool right) {
  d.setColor(UIColor::popup_bkg);

  if (right) {
    for (int i = 0; i < 3; i++) {
      fishLine(d,x-14,y-6+i, x-17,y-8+i);
      fishLine(d,x-14,y+6-i, x-17,y+8-i);
      fishLine(d,x-1+i,y-9,  x-3+i,y-11);
    }
  } else {
    for (int i = 0; i < 3; i++) {
      fishLine(d,x+14,y-6+i, x+17,y-8+i);
      fishLine(d,x+14,y+6-i, x+17,y+8-i);
      fishLine(d,x+1-i,y-9,  x+3-i,y-11);
    }
  }

  // Keep the known-good adult fish, then add longer flowing fins.
  drawAdultFishSprite(d,x,y,right);
  d.setColor(UIColor::primary_txt);

  if (right) {
    fishLine(d,x-14,y-6, x-17,y-8);
    fishLine(d,x-14,y+6, x-17,y+8);
    fishLine(d,x-1,y-9,  x-3,y-11);
  } else {
    fishLine(d,x+14,y-6, x+17,y-8);
    fishLine(d,x+14,y+6, x+17,y+8);
    fishLine(d,x+1,y-9,  x+3,y-11);
  }
}

static void drawFishSpriteForStage(DisplayDriver& d,int x,int y,bool right,uint8_t stage) {
  switch(stage) {
    case 0: drawFryFishSprite(d,x,y,right); break;
    case 1: drawYoungFishSprite(d,x,y,right); break;
    case 2: drawAdultFishSprite(d,x,y,right); break;
    default: drawElderFishSprite(d,x,y,right); break;
  }
}

static void drawFishSprite(DisplayDriver& d,int x,int y,bool right) {
  drawFishSpriteForStage(d,x,y,right,fish.evolution);
}

static void drawLoRaFishHeader(DisplayDriver& display, const char* title,
                               const char* right) {
  const int w=display.width();
  display.setColor(UIColor::primary_txt);
  display.setTextSize(1);
  display.setCursor(2,5);
  display.print(title);
  display.drawTextRightAlign(w-2,5,right);

  // Keep the separator visibly clear of the header glyphs.
  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,15,w-4,1);
}

static void drawLoRaFishEgg(DisplayDriver& display) {
  const int w=display.width(), h=display.height();
  char tb[8],db[24]; loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawLoRaFishHeader(display,loRaFishDisplayName(),tb);
  int wobble=((millis()/350UL)%3)-1;
  display.setColor(UIColor::primary_txt);
  // 14x18 pixel egg outline with a gentle wobble.
  display.drawRect(w/2-7+wobble,27,14,14);
  display.fillRect(w/2-5+wobble,24,10,2);
  display.fillRect(w/2-6+wobble,26,12,1);
  display.fillRect(w/2-5+wobble,41,10,2);
  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,h-15,w-4,1);
  display.drawTextCentered(w/2,h-9,"EGG - hatching soon");
}

static void drawLoRaFishMemorial(DisplayDriver& display) {
  const int w=display.width(), h=display.height();
  drawLoRaFishHeader(display,loRaFishDisplayName(),"REST");
  display.setColor(UIColor::primary_txt);
  // Small gravestone: deliberately simple/monochrome.
  display.fillRect(w/2-12,28,24,18);
  display.drawRect(w/2-9,22,18,25);
  display.drawTextCentered(w/2,29,"RIP");
  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,h-15,w-4,1);
  display.drawTextCentered(w/2,h-9,"Long press reset in OPTIONS");
}

static void drawLoRaFishOptions(DisplayDriver& display) {
  const int w=display.width();

  const char* right="";
  if (loRaFishOption==0) right="TIMEOUT";
  else if (loRaFishOption==1) right="RESET";
  else right="BACK";

  drawLoRaFishHeader(display,"OPTIONS",right);
  display.setColor(UIColor::primary_txt);

  display.setCursor(4,21);
  display.print(loRaFishOption==0 ? "> Screen timeout" : "  Screen timeout");
  display.setCursor(4,32);
  display.print(loRaFishOption==1 ? "> Reset pet" : "  Reset pet");
  display.setCursor(4,43);
  display.print(loRaFishOption==2 ? "> RF monitor" : "  RF monitor");

  display.setColor(UIColor::secondary_txt);
  if (loRaFishOption==0) {
    const char* labels[]={"15 seconds","30 seconds","60 seconds","Always on"};
    display.drawTextCentered(w/2,56,labels[loRaFishTimeoutSetting]);
  } else if (loRaFishOption==1) {
    display.drawTextCentered(w/2,56,"LONG PRESS to reset");
  } else {
    display.drawTextCentered(w/2,56,"LONG PRESS to open");
  }
}

static uint32_t loRaFishDXContactIndex=0;
static uint32_t loRaFishDXRequestStarted=0;
static uint32_t loRaFishDXCommitLastSent=0;
static uint8_t loRaFishDXCommitRetries=0;

static void serviceLoRaFishDXCommit() {
  if(!loragotchiDXHome.committing) {
    loRaFishDXCommitLastSent=0;
    loRaFishDXCommitRetries=0;
    return;
  }
  const uint32_t now=millis();
  // ACK handler sent the first COMMIT. Retry after 3 seconds, up to 4 times.
  if(loRaFishDXCommitLastSent==0) {
    loRaFishDXCommitLastSent=now;
    return;
  }
  if(now-loRaFishDXCommitLastSent < 3000UL) return;
  if(loRaFishDXCommitRetries >= 4) {
    Serial.println("[LFG1 PEER] ARRIVED timeout; fish kept HOME");
    loragotchiDXHome.committing=false;
    loragotchiDXHome.pending=false;
    loragotchiDXHome.away=false;
    loragotchiDXSave();
    loRaFishDXCommitLastSent=0;
    loRaFishDXCommitRetries=0;
    return;
  }
  ContactInfo dest;
  if(!the_mesh.getContactByIdx((uint32_t)MAX_ANON_CONTACTS + loragotchiDXHome.destinationIndex,dest)) return;
  char msg[80];
  snprintf(msg,sizeof(msg),"LFG1|COMMIT|%lu|%lu",
           (unsigned long)loragotchiDXHome.txid,(unsigned long)loragotchiDXHome.fishid);
  uint32_t ack=0,to=0;
  int r=the_mesh.sendMessage(dest,the_mesh.getRTCClock()->getCurrentTime(),0,msg,ack,to);
  loRaFishDXCommitLastSent=now;
  loRaFishDXCommitRetries++;
  Serial.printf("[LFG1 PEER] COMMIT RETRY %u TX result=%d\n",(unsigned)loRaFishDXCommitRetries,r);
}


static bool loRaFishDXGetContact(ContactInfo& c) {
  const int n=the_mesh.getNumContacts();
  if(n<=0) return false;
  if(loRaFishDXContactIndex >= (uint32_t)n) loRaFishDXContactIndex=0;

  // BaseChatMesh reserves the first MAX_ANON_CONTACTS slots for transient
  // contacts. getNumContacts() excludes them, but getContactByIdx() addresses
  // the underlying slot array, so offset into the persistent-contact region.
  const uint32_t rawIndex=(uint32_t)MAX_ANON_CONTACTS + loRaFishDXContactIndex;
  if(!the_mesh.getContactByIdx(rawIndex,c)) return false;

  // Don't present an empty/reserved entry as a DX destination.
  if(c.name[0]=='\\0') return false;
  return true;
}

static void drawLoRaFishDX(DisplayDriver& display) {
  const int w=display.width(),h=display.height();
  char tb[8],db[24]; loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawLoRaFishHeader(display,"DX VISIT",tb);
  display.setColor(UIColor::primary_txt);

  if(loragotchiDXHome.away) {
    display.drawTextCentered(w/2,27,"FISH AWAY");
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,39,loragotchiDXHome.destinationName);
    display.drawTextCentered(w/2,h-9,"LONG:RECALL  2X:OPTIONS");
  } else if(loragotchiDXHome.committing) {
    display.drawTextCentered(w/2,28,"DELIVERING...");
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,40,loragotchiDXHome.destinationName);
  } else if(loragotchiDXHome.pending) {
    if(loRaFishDXRequestStarted && millis()-loRaFishDXRequestStarted > 20000UL) {
      loragotchiDXHome.pending=false;
      loragotchiDXHome.away=false;
      loragotchiDXSave();
      loRaFishDXRequestStarted=0;
    }
    display.drawTextCentered(w/2,28,loragotchiDXHome.pending?"CALLING...":"NO REPLY - RETRY");
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,40,loragotchiDXHome.destinationName);
  } else {
    ContactInfo c;
    if(loRaFishDXGetContact(c)) {
      display.drawTextCentered(w/2,27,c.name);
      char b[28]; snprintf(b,sizeof(b),"%lu/%d  CLICK:NEXT",
          (unsigned long)(loRaFishDXContactIndex+1),the_mesh.getNumContacts());
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(w/2,39,b);
      display.drawTextCentered(w/2,h-9,"LONG:SEND  2X:OPTIONS");
    } else display.drawTextCentered(w/2,31,"NO CONTACTS");
  }
}

static void drawLoRaFishRFMonitor(DisplayDriver& display, UITask* task) {
  const int w=display.width(), h=display.height();

  // Capture one history point per actual MeshCore RX sample.
  if (lorafish_rf_sample_seq != loRaFishRfSeenSeq) {
    loRaFishRfSeenSeq = lorafish_rf_sample_seq;
    int v=(int)lorafish_last_rssi;
    if (v < -127) v=-127;
    if (v > -20) v=-20;
    if (loRaFishRfHistoryCount < 48) {
      loRaFishRfHistory[loRaFishRfHistoryCount++]=(int8_t)v;
    } else {
      memmove(&loRaFishRfHistory[0],&loRaFishRfHistory[1],47);
      loRaFishRfHistory[47]=(int8_t)v;
    }
  }

  char right[16];
  uint16_t mv=task->getBattMilliVolts();
  int batt=(int)((mv-3300L)*100L/(4200L-3300L));
  batt=constrain(batt,0,100);
  snprintf(right,sizeof(right),"BAT %d%%",batt);
  drawLoRaFishHeader(display,"RF MONITOR",right);

  display.setColor(UIColor::primary_txt);
  char line[32];
  snprintf(line,sizeof(line),"RSSI %.0f  SNR %.1f",lorafish_last_rssi,lorafish_last_snr);
  display.setCursor(3,20);
  display.print(line);

  // 48-column RSSI history graph. -120 dBm is bottom, -40 dBm is top.
  const int gx=3, gy=31, gw=min(48,w-6), gh=20;
  display.setColor(UIColor::secondary_txt);
  display.fillRect(gx,gy+gh,gw,1);
  display.fillRect(gx,gy,1,gh+1);
  int start=max(0,(int)loRaFishRfHistoryCount-gw);
  for(int i=start;i<(int)loRaFishRfHistoryCount;i++) {
    int r=loRaFishRfHistory[i];
    int level=constrain((r+120)*gh/80,0,gh);
    int x=gx+(i-start);
    display.fillRect(x,gy+gh-level,1,max(1,level));
  }

  display.setColor(UIColor::secondary_txt);
  if (lorafish_last_rf_ms==0) {
    display.drawTextRightAlign(w-2,55,"RX --");
  } else {
    unsigned long age=(millis()-lorafish_last_rf_ms)/1000UL;
    snprintf(line,sizeof(line),"RX %lus",age);
    display.drawTextRightAlign(w-2,55,line);
  }

  // Graphical battery gauge.
  int bx=3, by=55, bw=38, bh=7;
  display.drawRect(bx,by,bw,bh);
  display.fillRect(bx+bw,by+2,2,3);
  int fill=(batt*(bw-4))/100;
  if(fill>0) display.fillRect(bx+2,by+2,fill,bh-4);
}


static void drawLoRaFishBubbles(DisplayDriver& display) {
  const int h = display.height();

  // Different speeds and starting positions so they don't move together.
  int y1 = h - 10 - ((millis() / 180) % 35);
  int y2 = h - 10 - ((millis() / 260 + 13) % 38);
  int y3 = h - 10 - ((millis() / 340 + 25) % 36);

  // Gentle sideways wobble.
  int x1 = 15 + (int)(2.0f * sinf(millis() / 700.0f));
  int x2 = 29 + (int)(2.0f * sinf(millis() / 900.0f));
  int x3 = display.width() - 18 +
           (int)(2.0f * sinf(millis() / 800.0f));

  display.setColor(UIColor::secondary_txt);

  display.drawRect(x1, y1, 2, 2);
  display.drawRect(x2, y2, 3, 3);
  display.drawRect(x3, y3, 2, 2);
}


static void drawLoRaFishPoop(DisplayDriver& display, int x, int y, uint8_t phase) {
  display.setColor(UIColor::primary_txt);
  int wobble = phase & 1;

  // v0.3.5.3: gloriously oversized 9x8 Tamagotchi-style pile.
  display.fillRect(x+4+wobble, y,   2,1);
  display.fillRect(x+3,        y+1, 4,1);
  display.fillRect(x+2,        y+2, 5,1);
  display.fillRect(x+1,        y+3, 7,2);
  display.fillRect(x,          y+5, 9,2);
  display.fillRect(x+1,        y+7, 7,1);

  // Three-phase animated stink trails.
  uint8_t stink = (millis()/280UL + phase) % 3;
  if (stink == 0) {
    display.fillRect(x+1,y-5,1,2);
    display.fillRect(x+2,y-7,1,2);
    display.fillRect(x+6,y-6,1,2);
    display.fillRect(x+7,y-8,1,2);
  } else if (stink == 1) {
    display.fillRect(x+2,y-5,1,2);
    display.fillRect(x+1,y-7,1,2);
    display.fillRect(x+7,y-6,1,2);
    display.fillRect(x+6,y-8,1,2);
  } else {
    display.fillRect(x+1,y-6,1,2);
    display.fillRect(x+2,y-8,1,2);
    display.fillRect(x+6,y-5,1,2);
    display.fillRect(x+7,y-7,1,2);
  }
}

static void drawLoRaFishHygiene(DisplayDriver& display) {
  const int w=display.width(), h=display.height();
  const int floorY=h-20;
  uint8_t wiggle=(millis()/350UL)&1;

  static const int poopX[3]={20,58,96};
  for(uint8_t i=0;i<loRaFishPoopCount && i<LORAFISH_MAX_POOP;i++)
    drawLoRaFishPoop(display,poopX[i],floorY-8,wiggle ^ (i&1));

  if (loRaFishCleaning) {
    float p=(millis()-loRaFishCleaningStarted)/(float)LORAFISH_CLEAN_MS;
    if(p>1.0f) p=1.0f;
    int bx=-8+(int)((w+10)*p);

    // Tiny broom: diagonal handle + 7px brush.
    display.setColor(UIColor::primary_txt);
    fishLine(display,bx+5,floorY-15,bx,floorY-5);
    display.fillRect(bx-3,floorY-5,8,2);
    display.fillRect(bx-2,floorY-3,1,2);
    display.fillRect(bx,floorY-3,1,2);
    display.fillRect(bx+2,floorY-3,1,2);
    display.fillRect(bx+4,floorY-3,1,2);

    // Poop already passed by the broom visually disappears.
    for(uint8_t i=0;i<loRaFishPoopCount && i<LORAFISH_MAX_POOP;i++) {
      if (poopX[i]+9 > bx+5)
        drawLoRaFishPoop(display,poopX[i],floorY-8,wiggle ^ (i&1));
    }
  }
}

static bool loRaFishIsSleeping();

static void drawLoRaFishAquarium(DisplayDriver& display, UITask* task) {
  const int w=display.width(), h=display.height();
  char tb[8], db[24];
  loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawLoRaFishHeader(display,loRaFishDisplayName(),tb);

  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,h-15,w-4,1);
  drawLoRaFishBubbles(display);
  //display.drawRect(15,21,2,2);
  //display.drawRect(24,31,3,3);
  //display.drawRect(w-18,20,2,2);
  //display.fillRect(6,h-16,2,6);
  //display.fillRect(w-9,h-18,2,8);

  int x=0, y=0;
  bool right=true;
  loRaFishNaturalPosition(w,x,y,right);

  if (loRaFishFeeding) {
    unsigned long elapsed = millis() - loRaFishFeedStarted;
    const int foodX = w/2 + 18;
    float p = elapsed / (float)LORAFISH_FEED_DROP_MS;
    if (p > 1.0f) p = 1.0f;

    const int targetX = foodX - 15;
    x = loRaFishFeedStartX + (int)((targetX - loRaFishFeedStartX) * p);
    y = loRaFishFeedStartY + (int)((35 - loRaFishFeedStartY) * p)
        + (int)sinf(millis()/180.0f);
    right = (targetX >= loRaFishFeedStartX);

    if (elapsed < LORAFISH_FEED_DROP_MS) {
      int foodY = 18 + (int)((34 - 18) * p);
      display.setColor(UIColor::primary_txt);
      display.fillRect(foodX, foodY, 2, 2);
    } else if (!loRaFishFeedPipped) {
      loRaFishFeedPipped = true;
      // Meal was already credited when the RF threshold was reached.
      // This is only the visible/audible eating feedback.

    }

    if (elapsed >= LORAFISH_FEED_DONE_MS) loRaFishFeeding = false;
  }

 drawLoRaFishHygiene(display);
  const bool localSleeping = loRaFishIsSleeping();

  if (!loragotchiDXHome.away) {
    // A guest must not freeze the host fish.  The home fish keeps its normal
    // swim path; if it is bedtime, only the home fish sleeps.
    if (localSleeping) {
      const int sleepX = loragotchiDXGuest.present ? w/3 : w/2-5;
      const int sleepY = 38;
      drawFishSprite(display,sleepX,sleepY,true);
      display.setColor(UIColor::primary_txt);
      display.setCursor(min(w-4,sleepX+12), max(17,sleepY-11)); display.print("z");
      display.setCursor(min(w-4,sleepX+19), max(12,sleepY-16)); display.print("Z");
    } else {
      drawFishSprite(display,x,y,right);
    }
  } else if (!loragotchiDXGuest.present) {
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,35,"FISH DX AWAY");
  }

  if (loragotchiDXGuest.present) {
    // Same movement model as a lone fish, phase shifted by half a cycle.
    // This keeps the visitor inside the tank and removes the old erratic
    // full-screen sweep.
    int gx=0,gy=0; bool gright=true;
    unsigned long guestPeriod=8000UL;
    if(loragotchiDXGuest.stage==0) guestPeriod=5200UL;
    else if(loragotchiDXGuest.stage==1) guestPeriod=6500UL;
    else if(loragotchiDXGuest.stage>=3) guestPeriod=11000UL;
    loRaFishNaturalPositionForStage(w,loragotchiDXGuest.stage,guestPeriod/2,gx,gy,gright);
    drawFishSpriteForStage(display,gx,gy,gright,loragotchiDXGuest.stage);
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,27,loragotchiDXGuest.guestName);
  }

  if (millis() < loRaFishEvolutionFlashUntil) {
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,19,"* EVOLVED! *");
  } else if (fishReaction==1) display.drawTextCentered(w/2,19,"* MESSAGE *");
  else if (fishReaction==2) display.drawTextCentered(w/2,19,"* SENT *");
  else if (fishReaction==3) display.drawTextCentered(w/2,19,"* HELLO! *");

  char bottom[48];
  snprintf(bottom,sizeof(bottom),"%s  RF:%lu %s",
           fishStageName(),(unsigned long)fish.rfEvents,
           task->hasConnection()?"PHONE":"");
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(w/2,h-9,bottom);
}

static void drawLoRaFishStats(DisplayDriver& display, UITask* task) {
  const int w=display.width();
  drawLoRaFishHeader(display,"STATS",fishStageName());
  display.setColor(UIColor::primary_txt);
  char statLine[48];

  snprintf(statLine,sizeof(statLine),"Age  %luh %02lum",
           (unsigned long)(fish.ageMinutes/60UL),
           (unsigned long)(fish.ageMinutes%60UL));
  display.setCursor(2,21);
  display.print(statLine);

  snprintf(statLine,sizeof(statLine),"Happy %u%%",(unsigned)fish.happiness);
  display.setCursor(2,31);
  display.print(statLine);

  snprintf(statLine,sizeof(statLine),"RF %lu  Msg %lu",
           (unsigned long)fish.rfEvents,
           (unsigned long)fish.messages);
  display.setCursor(2,41);
  display.print(statLine);

  snprintf(statLine,sizeof(statLine),"ACK %lu  BLE %s",
           (unsigned long)fish.txAcks,
           task->hasConnection()?"ON":"OFF");
  display.setCursor(2,51);
  display.print(statLine);
  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,60,w-4,1);
}

static void drawLoRaFishClockPage(DisplayDriver& display, UITask* task) {
  const int w=display.width();
  char tb[8], db[24];
  bool meshTime = loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawLoRaFishHeader(display,"CLOCK",task->hasConnection()?"PHONE":"MESH");

  display.setColor(UIColor::primary_txt);
  display.setTextSize(2);
  display.drawTextCentered(w/2,26,tb);
  display.setTextSize(1);
  display.drawTextCentered(w/2,44,db);
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(w/2,55,meshTime ? "MeshCore RTC (UTC)" : "build-time fallback");
}

static void drawLoRaFishPairing(DisplayDriver& display) {
  const int w=display.width();
  drawLoRaFishHeader(display,"PHONE PAIRING","BLE");

  uint32_t activePin = the_mesh.getBLEPin();

  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(w/2,20,"MeshCore Companion");

  char pin[16];
  if (activePin != 0) {
    snprintf(pin,sizeof(pin),"%06lu",(unsigned long)activePin);
    display.setTextSize(2);
    display.drawTextCentered(w/2,30,pin);
    display.setTextSize(1);
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,45,"PIN for phone");
    display.drawTextCentered(w/2,55,"ENTER: next");
  } else {
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,32,"No PIN configured");
    display.drawTextCentered(w/2,47,"BLE security off");
    display.drawTextCentered(w/2,63,"ENTER: next");
  }
}

static bool loRaFishIsSleeping() {
  char tb[8], db[24];
  loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  int hh = atoi(tb);
  return hh >= LORAFISH_SLEEP_HOUR || hh < LORAFISH_WAKE_HOUR;
}

static void drawLoRaFishSleeping(DisplayDriver& display, UITask* task) {
  const int w=display.width(), h=display.height();
  char tb[8], db[24];
  loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawLoRaFishHeader(display,loRaFishDisplayName(),tb);

 drawFishSprite(display,w/2-5,38,true);

display.setColor(UIColor::primary_txt);

display.setCursor(w/2+12, 27);
display.print("z");

display.setCursor(w/2+19, 22);
display.print("Z");

display.setCursor(w/2+27, 17);
display.print("Z");

display.setColor(UIColor::secondary_txt);
display.fillRect(2,h-15,w-4,1);
display.drawTextCentered(w/2,h-9,"Sleeping");
}

static void drawLoRaFish(DisplayDriver& display, UITask* task) {
  if (millis() >= fishReactionUntil) fishReaction=0;

  const bool sleeping = loRaFishIsSleeping();

  switch (loRaFishPage) {
    case 1: drawLoRaFishStats(display,task); break;
    case 2: drawLoRaFishClockPage(display,task); break;
    case 3: drawLoRaFishPairing(display); break;
    case 4: drawLoRaFishDX(display); break;
    case 5: drawLoRaFishOptions(display); break;
    case 6: drawLoRaFishRFMonitor(display,task); break;
    default:
      if (fish.ageMinutes >= LORAFISH_LIFESPAN_MINUTES) drawLoRaFishMemorial(display);
      else if (fish.ageMinutes < LORAFISH_EGG_MINUTES) drawLoRaFishEgg(display);
      // DX away state takes priority over the local sleep schedule.
      // drawLoRaFishAquarium() already suppresses the local sprite and shows
      // FISH DX AWAY while loragotchiDXHome.away is true.
      else if (loragotchiDXHome.away || loragotchiDXGuest.present) drawLoRaFishAquarium(display,task);
      else if (sleeping) drawLoRaFishSleeping(display,task);
      else drawLoRaFishAquarium(display,task);
      break;
  }
}

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // strip off dash and commit hash by changing dash to null terminator
    // e.g: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }


  //=============== startup screen ====================================
  int render(DisplayDriver& display) override {
    // LoRaGotchi DX v1.0 splash. Render the title with the display font rather
    // than an XBM logo so it stays crisp on both M2 and Heltec V3 OLEDs.
    const int cx = display.width() / 2;
    display.setColor(UIColor::primary_txt);
    display.setTextSize(2);
    display.drawTextCentered(cx, 3, "LoRaGotchi");

    // Tiny pixel-fish motif, deliberately kept inside the title area.
    display.setTextSize(1);
    display.drawTextCentered(cx, 21, "><((o>");
    display.drawTextCentered(cx, 33, "DX v1.0");

    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(cx, 44, "M0YNW");
    display.drawTextCentered(cx, 54, "Powered by MeshCore");

    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }
};

class HomeScreen : public UIScreen {
  enum HomePage {
    FIRST,
    RECENT,
    RADIO,
    BLUETOOTH,
    ADVERT,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
    SHUTDOWN,
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _shutdown_init;
  AdvertPath recent[UI_RECENT_LIST_SIZE];


  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
    // Convert millivolts to percentage
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif
    const int minMilliVolts = BATT_MIN_MILLIVOLTS;
    const int maxMilliVolts = BATT_MAX_MILLIVOLTS;
    int batteryPercentage = ((batteryMilliVolts - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts);
    if (batteryPercentage < 0) batteryPercentage = 0; // Clamp to 0%
    if (batteryPercentage > 100) batteryPercentage = 100; // Clamp to 100%

    // battery icon
    int iconWidth = 24;
    int iconHeight = 10;
    int iconX = display.width() - iconWidth - 5; // Position the icon near the top-right corner
    int iconY = 0;
    display.setColor(UIColor::title_txt);

    // battery outline
    display.drawRect(iconX, iconY, iconWidth, iconHeight);

    // battery "cap"
    display.fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 3, iconHeight / 2);

    // fill the battery based on the percentage
    int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
    display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);

    // show muted icon if buzzer is muted
#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(UIColor::warning_txt);
      display.drawXbm(iconX - 9, iconY + 1, muted_icon, 8, 8);
    }
#endif
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  bool sensors_scroll = false;
  int sensors_scroll_offset = 0;
  int next_sensors_refresh = 0;

  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
      sensors_scroll = sensors_nb > UI_RECENT_LIST_SIZE;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0),
       _shutdown_init(false), sensors_lpp(200) {
#ifdef LORAFISH_UI
    loRaFishRTC = rtc;
#endif
  }

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
#ifdef LORAFISH_UI
    drawLoRaFish(display, _task);
    return 100; // smooth fish animation without blocking MeshCore
#else
    display.setColor(UIColor::title_bkg);
    display.fillRect(0, 0, display.width(), 12);
    char tmp[80];
    // node name
    display.setTextSize(1);
    display.setColor(UIColor::title_txt);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
    display.setCursor(0, 2);
    display.print(filtered_name);

    // battery voltage
    renderBatteryIndicator(display, _task->getBattMilliVolts());

    // curr page indicator
    if (UIColor::title_bkg == UIColor::window_bkg) {
      display.setColor(UIColor::title_txt);
    } else {
      display.setColor(UIColor::title_bkg);
    }
    int y = 14;
    int x = display.width() / 2 - 5 * (HomePage::Count-1);
    for (uint8_t i = 0; i < HomePage::Count; i++, x += 10) {
      if (i == _page) {
        display.fillRect(x-1, y-1, 4, 4);
      } else {
        display.fillRect(x, y, 2, 2);
      }
    }

    if (_page == HomePage::FIRST) {
      display.setColor(UIColor::primary_txt);
      display.setTextSize(2);
      sprintf(tmp, "MSG: %d", _task->getMsgCount());
      display.drawTextCentered(display.width() / 2, 22, tmp);

      #ifdef WIFI_SSID
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 54, tmp);
      #endif
      if (_task->hasConnection()) {
        display.setColor(UIColor::warning_txt);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 43, "< Connected >");

      } else if (the_mesh.getBLEPin() != 0) { // BT pin
        display.setColor(UIColor::warning_txt);
        display.setTextSize(2);
        sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, 43, tmp);
      }
    } else if (_page == HomePage::RECENT) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      display.setColor(UIColor::primary_txt);
      int y = 20;
      for (int i = 0; i < UI_RECENT_LIST_SIZE; i++, y += 11) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          sprintf(tmp, "%ds", secs);
        } else if (secs < 60*60) {
          sprintf(tmp, "%dm", secs / 60);
        } else {
          sprintf(tmp, "%dh", secs / (60*60));
        }

        int timestamp_width = display.getTextWidth(tmp);
        int max_name_width = display.width() - timestamp_width - 1;

        char filtered_recent_name[sizeof(a->name)];
        display.translateUTF8ToBlocks(filtered_recent_name, a->name, sizeof(filtered_recent_name));
        display.drawTextEllipsized(0, y, max_name_width, filtered_recent_name);
        display.setCursor(display.width() - timestamp_width - 1, y);
        display.print(tmp);
      }
    } else if (_page == HomePage::RADIO) {
      display.setColor(UIColor::primary_txt);
      display.setTextSize(1);
      // freq / sf
      display.setCursor(0, 20);
      sprintf(tmp, "FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
      display.print(tmp);

      display.setCursor(0, 31);
      sprintf(tmp, "BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
      display.print(tmp);

      // tx power,  noise floor
      display.setCursor(0, 42);
      sprintf(tmp, "TX: %ddBm", _node_prefs->tx_power_dbm);
      display.print(tmp);
      display.setCursor(0, 53);
      sprintf(tmp, "Noise floor: %d", radio_driver.getNoiseFloor());
      display.print(tmp);
    } else if (_page == HomePage::BLUETOOTH) {
      display.setColor(UIColor::corp_blue);
      display.drawXbm((display.width() - 32) / 2, 18,
          _task->isBluetoothEnabled() ? bluetooth_on : bluetooth_off,
          32, 32);
      display.setColor(UIColor::secondary_txt);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 64 - 11, "toggle: " PRESS_LABEL);
    } else if (_page == HomePage::ADVERT) {
      display.setColor(UIColor::corp_blue);
      display.drawXbm((display.width() - 32) / 2, 18, advert_icon, 32, 32);
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(display.width() / 2, 64 - 11, "advert: " PRESS_LABEL);
#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      int y = 18;
      bool gps_state = _task->getGPSState();
#ifdef PIN_GPS_SWITCH
      bool hw_gps_state = digitalRead(PIN_GPS_SWITCH);
      if (gps_state != hw_gps_state) {
        strcpy(buf, gps_state ? "gps off(hw)" : "gps off(sw)");
      } else {
        strcpy(buf, gps_state ? "gps on" : "gps off");
      }
#else
      strcpy(buf, gps_state ? "gps on" : "gps off");
#endif
      display.setColor(UIColor::primary_txt);
      display.drawTextLeftAlign(0, y, buf);
      if (nmea == NULL) {
        y = y + 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "Can't access GPS");
      } else {
        display.setColor(UIColor::primary_txt);
        strcpy(buf, nmea->isValid()?"fix":"no fix");
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "sat");
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "pos");
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%.4f %.4f",
          nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "alt");
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%.2f", nmea->getAltitude()/1000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
      }
#endif
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      int y = 18;
      refresh_sensors();
      char buf[30];
      char name[30];
      LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

      for (int i = 0; i < sensors_scroll_offset; i++) {
        uint8_t channel, type;
        r.readHeader(channel, type);
        r.skipData(type);
      }

      for (int i = 0; i < (sensors_scroll?UI_RECENT_LIST_SIZE:sensors_nb); i++) {
        uint8_t channel, type;
        if (!r.readHeader(channel, type)) { // reached end, reset
          r.reset();
          r.readHeader(channel, type);
        }

        display.setCursor(0, y);
        float v;
        switch (type) {
          case LPP_GPS: // GPS
            float lat, lon, alt;
            r.readGPS(lat, lon, alt);
            strcpy(name, "gps"); sprintf(buf, "%.4f %.4f", lat, lon);
            break;
          case LPP_VOLTAGE:
            r.readVoltage(v);
            strcpy(name, "voltage"); sprintf(buf, "%6.2f", v);
            break;
          case LPP_CURRENT:
            r.readCurrent(v);
            strcpy(name, "current"); sprintf(buf, "%.3f", v);
            break;
          case LPP_TEMPERATURE:
            r.readTemperature(v);
            strcpy(name, "temperature"); sprintf(buf, "%.2f", v);
            break;
          case LPP_RELATIVE_HUMIDITY:
            r.readRelativeHumidity(v);
            strcpy(name, "humidity"); sprintf(buf, "%.2f", v);
            break;
          case LPP_BAROMETRIC_PRESSURE:
            r.readPressure(v);
            strcpy(name, "pressure"); sprintf(buf, "%.2f", v);
            break;
          case LPP_ALTITUDE:
            r.readAltitude(v);
            strcpy(name, "altitude"); sprintf(buf, "%.0f", v);
            break;
          case LPP_POWER:
            r.readPower(v);
            strcpy(name, "power"); sprintf(buf, "%6.2f", v);
            break;
          default:
            r.skipData(type);
            strcpy(name, "unk"); sprintf(buf, "");
        }
        display.setCursor(0, y);
        display.setColor(UIColor::secondary_txt);
        display.print(name);
        display.setColor(UIColor::primary_txt);
        display.setCursor(
          display.width()-display.getTextWidth(buf)-1, y
        );
        display.print(buf);
        y = y + 12;
      }
      if (sensors_scroll) sensors_scroll_offset = (sensors_scroll_offset+1)%sensors_nb;
      else sensors_scroll_offset = 0;
#endif
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(UIColor::corp_blue);
      display.setTextSize(1);
      if (_shutdown_init) {
        display.setColor(UIColor::warning_txt);
        display.drawTextCentered(display.width() / 2, 34, "hibernating...");
      } else {
        display.setColor(UIColor::secondary_txt);
        display.drawXbm((display.width() - 32) / 2, 18, power_icon, 32, 32);
        display.drawTextCentered(display.width() / 2, 64 - 11, "hibernate:" PRESS_LABEL);
      }
    }
    return 5000;   // next render after 5000 ms
#endif
  }

  bool handleInput(char c) override {
#ifdef LORAFISH_UI
    // ThinkNode's single user button reports a short click as KEY_NEXT.
    // Joystick-style targets may report KEY_ENTER, so accept either.
    if (c == KEY_ENTER || c == KEY_NEXT) {
      if (loRaFishPage == 5) {
        loRaFishOption = (loRaFishOption + 1) % 3;
      } else if (loRaFishPage == 4 && !loragotchiDXHome.away && !loragotchiDXHome.pending) {
        int n=the_mesh.getNumContacts();
        if(n>0) loRaFishDXContactIndex=(loRaFishDXContactIndex+1)%n;
      } else {
        loRaFishPage = (loRaFishPage + 1) % LORAFISH_PAGE_COUNT;
      }
      return true;
    }
    return true;
#else
    if (c == KEY_LEFT || c == KEY_PREV) {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _page = (_page + 1) % HomePage::Count;
      if (_page == HomePage::RECENT) {
        _task->showAlert("Recent adverts", 800);
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      if (_task->isBluetoothEnabled()) {  // toggle Bluetooth on/off
        _task->disableBluetooth();
      } else {
        _task->enableBluetooth();
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::ADVERT) {
      _task->notify(UIEventType::ack);
      if (the_mesh.advert()) {
        _task->showAlert("Advert sent!", 1000);
      } else {
        _task->showAlert("Advert failed..", 1000);
      }
      return true;
    }
#if ENV_INCLUDE_GPS == 1
    if (c == KEY_ENTER && _page == HomePage::GPS) {
      _task->toggleGPS();
      return true;
    }
#endif
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && _page == HomePage::SENSORS) {
      _task->toggleGPS();
      next_sensors_refresh=0;
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::SHUTDOWN) {
      _shutdown_init = true;  // need to wait for button to be released
      return true;
    }
    return false;
#endif
  }};

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[78];
  };
  #define MAX_UNREAD_MSGS   32
  int num_unread;
  int head = MAX_UNREAD_MSGS - 1; // index of latest unread message
  MsgEntry unread[MAX_UNREAD_MSGS];

public:
  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { num_unread = 0; }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg) {
    head = (head + 1) % MAX_UNREAD_MSGS;
    if (num_unread < MAX_UNREAD_MSGS) num_unread++;

    auto p = &unread[head];
    p->timestamp = _rtc->getCurrentTime();
    if (path_len == 0xFF) {
      sprintf(p->origin, "(D) %s:", from_name);
    } else {
      sprintf(p->origin, "(%d) %s:", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& display) override {
    char tmp[16];
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(UIColor::corp_blue);
    sprintf(tmp, "Unread: %d", num_unread);
    display.print(tmp);

    auto p = &unread[head];

    int secs = _rtc->getCurrentTime() - p->timestamp;
    if (secs < 60) {
      sprintf(tmp, "%ds", secs);
    } else if (secs < 60*60) {
      sprintf(tmp, "%dm", secs / 60);
    } else {
      sprintf(tmp, "%dh", secs / (60*60));
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);  // horiz line

    display.setCursor(0, 14);
    display.setColor(UIColor::secondary_txt);
    char filtered_origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(filtered_origin, p->origin, sizeof(filtered_origin));
    display.print(filtered_origin);

    display.setCursor(0, 25);
    display.setColor(UIColor::primary_txt);
    char filtered_msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
    display.printWordWrap(filtered_msg, display.width());

#if AUTO_OFF_MILLIS==0 // probably e-ink
    return 10000; // 10 s
#else
    return 1000;  // next render after 1000 ms
#endif
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
      num_unread--;
      if (num_unread == 0) {
        _task->gotoHomeScreen();
      }
      return true;
    }
    if (c == KEY_ENTER) {
      num_unread = 0;  // clear unread queue
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {

  _display = display;
  _sensors = sensors;
  #ifdef LORAFISH_UI
  _auto_off = loRaFishScreenTimeoutMs() ? millis() + loRaFishScreenTimeoutMs() : 0xFFFFFFFFUL;
#else
  _auto_off = millis() + AUTO_OFF_MILLIS;
#endif

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  _node_prefs = node_prefs;
#ifdef LORAFISH_UI
  loRaFishMeshNodeName = node_prefs ? node_prefs->node_name : nullptr;
#endif

  if (_display != NULL) {
    _display->turnOn();
  }

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
  buzzer.startup();
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
#ifdef LORAFISH_UI
  loadLoRaFish();
  loadLoRaFishSettings();
  loRaFishBootMs = millis();
#endif
  _alert_expiry = 0;

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  setCurrScreen(splash);
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::notify(UIEventType t) {
#ifdef LORAFISH_UI
  switch (t) {
    case UIEventType::contactMessage:
    case UIEventType::channelMessage:
    case UIEventType::roomMessage:
      reactLoRaFish(1);
      break;
    case UIEventType::ack:
      fish.txAcks++;
      reactLoRaFish(2);
      break;
    case UIEventType::newContactMessage:
      reactLoRaFish(3);
      break;
    default:
      break;
  }
#endif

#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}


void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0) {
    gotoHomeScreen();
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;
#ifdef LORAFISH_UI
  fish.messages++;
  reactLoRaFish(1);
  saveLoRaFish();
  gotoHomeScreen();

  if (_display != NULL) {
    // LoRaFish notifications are background events. Never wake the OLED just
    // because a MeshCore message arrived; refresh only if it is already on.
    if (_display->isOn()) {
      _auto_off = loRaFishScreenTimeoutMs() ? millis() + loRaFishScreenTimeoutMs() : 0xFFFFFFFFUL;
      _next_refresh = 0;
    }
  }
#else
  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from_name, text);
  setCurrScreen(msg_preview);

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) _display->turnOn();
    if (_display->isOn()) {
      #ifdef LORAFISH_UI
  _auto_off = loRaFishScreenTimeoutMs() ? millis() + loRaFishScreenTimeoutMs() : 0xFFFFFFFFUL;
#else
  _auto_off = millis() + AUTO_OFF_MILLIS;
#endif
      _next_refresh = 100;
    }
  }
#endif
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  curr = c;
  _next_refresh = 100;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    // Power off board including radio, display, GPS and components
    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
#ifdef LORAFISH_UI
  tickLoRaFish();
  serviceLoRaFishDXCommit();

  // OLED sleep and PET sleep are different:
  // - OLED off: gameplay continues normally and eating still beeps.
  // - Scheduled pet sleep (21:00-07:00): RF is counted, but the sleeping fish
  //   does not eat. Consume earned-feed thresholds so meals do not pile up
  //   overnight and suddenly fire when the fish wakes.
  const bool petSleeping = loRaFishIsSleeping();
  while (fish.rfEvents >= loRaFishNextAutoFeed) {
    if (!petSleeping) {
      const bool canAnimate = _display && _display->isOn() &&
                              loRaFishPage == 0 && !loRaFishFeeding;
      creditLoRaFishMeal(canAnimate, _display ? _display->width() : 128);
      // Eating sound is gameplay feedback, independent of OLED state.
      playFeedPip();
    }
    loRaFishNextAutoFeed += LORAFISH_PACKETS_PER_FEED;
  }

  if (tickLoRaFishPoop()) {
    playCleanMelody();
  }

  if (fish.ageMinutes >= LORAFISH_EGG_MINUTES &&
      fish.ageMinutes < LORAFISH_EGG_MINUTES + 2 &&
      !loRaFishHatchMelodyPlayed) {
    playHatchMelody();
    loRaFishHatchMelodyPlayed = true;
  }
#endif
  char c = 0;
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);  // REVISIT: could be mapped to different key code
  }
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#endif
#if defined(UI_HAS_ROTARY_INPUT)
  RotaryInputEvent rotaryEv = rotary_input.poll();
  if (c == 0 && _display != NULL && _display->isOn()) {
    if (rotaryEv == RotaryInputEvent::Next) {
      c = KEY_NEXT;
    } else if (rotaryEv == RotaryInputEvent::Prev) {
      c = KEY_PREV;
    }
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    int ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  if (c != 0 && curr) {
    curr->handleInput(c);
    #ifdef LORAFISH_UI
  _auto_off = loRaFishScreenTimeoutMs() ? millis() + loRaFishScreenTimeoutMs() : 0xFFFFFFFFUL;
#else
  _auto_off = millis() + AUTO_OFF_MILLIS;
#endif   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(UIColor::popup_bkg);
        _display->fillRect(p, y, _display->width() - p*2, y);
        _display->setColor(UIColor::popup_txt);  // draw box border
        _display->drawRect(p, y, _display->width() - p*2, y);
        _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
#ifdef KEEP_DISPLAY_ON_USB
    // Opt-in: refresh the auto-off deadline while externally powered, so the
    // timer counts from the moment external power is removed. Off by default
    // because OLED panels burn in quickly; only enable for LCD targets or
    // where the display is replaceable.
    if (board.isExternalPowered()) {
      #ifdef LORAFISH_UI
  _auto_off = loRaFishScreenTimeoutMs() ? millis() + loRaFishScreenTimeoutMs() : 0xFFFFFFFFUL;
#else
  _auto_off = millis() + AUTO_OFF_MILLIS;
#endif
    }
#endif
    #ifdef LORAFISH_UI
    if (loRaFishScreenTimeoutMs() != 0 && millis() > _auto_off) {
#else
    if (millis() > _auto_off) {
#endif
      _display->turnOff();
    }
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {
      if(!board.isExternalPowered()) {
        if (_display != NULL) {
          _display->startFrame();
          _display->setTextSize(2);
          _display->setColor(UIColor::warning_txt);
          _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
          _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
          _display->endFrame();
          if (_display->isEink() == false) { delay(3000); }
        }
        shutdown();
      }
    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();   // turn display on and consume event
      c = 0;
    }
    #ifdef LORAFISH_UI
  _auto_off = loRaFishScreenTimeoutMs() ? millis() + loRaFishScreenTimeoutMs() : 0xFFFFFFFFUL;
#else
  _auto_off = millis() + AUTO_OFF_MILLIS;
#endif   // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
  }
  return c;
}

char UITask::handleLongPress(char c) {
  // Keep MeshCore's startup rescue gesture.
  if (millis() - ui_started_at < 8000) {
    the_mesh.enterCLIRescue();
    return 0;
  }

#ifdef LORAFISH_UI
  if (loRaFishPage == 0) {
    if (loRaFishPoopCount > 0 && !loRaFishCleaning) {
      startLoRaFishCleaning();
      showAlert("Cleaning tank!", 450);
      _next_refresh = 0;
    }
    // Consume aquarium long press so MeshCore cannot treat it as another action.
    return 0;
  }

  if (loRaFishPage == 4) {
    ContactInfo c;
    if(!loRaFishDXGetContact(c)) { showAlert("No DX contact",700); return 0; }
    uint32_t ack=0,to=0;
    if(loragotchiDXHome.away || loragotchiDXHome.committing) {
      // Recall the exact contact used for the visit, even if ARRIVED confirmation is still pending.
      // This provides a safe recovery path if the final confirmation is lost.
      ContactInfo dest;
      if(the_mesh.getContactByIdx((uint32_t)MAX_ANON_CONTACTS + loragotchiDXHome.destinationIndex,dest)) c=dest;
      char msg[80]; snprintf(msg,sizeof(msg),"LFG1|RECALL|%lu|%lu",
        (unsigned long)loragotchiDXHome.txid,(unsigned long)loragotchiDXHome.fishid);
      int r=the_mesh.sendMessage(c,the_mesh.getRTCClock()->getCurrentTime(),0,msg,ack,to);
      Serial.printf("[LFG1 M2] RECALL TX result=%d\n",r);
      showAlert("Recalling fish",700);
    } else if(!loragotchiDXHome.pending && !loragotchiDXHome.committing) {
      // Sleeping fish stay home.  Recall remains available above so an
      // already-away fish can always be recovered at night.
      if (loRaFishIsSleeping()) {
        showAlert("Fish is sleeping",900);
        Serial.println("[LFG1 PEER] SEND BLOCKED: local fish is sleeping");
        _next_refresh=0;
        return 0;
      }
      if(!loragotchiDXHome.fishid) {
        loragotchiDXHome.fishid =
            0x4C460000UL ^
            ((uint32_t)fish.ageMinutes << 8) ^
            (uint32_t)fish.rfEvents ^
            (uint32_t)millis();
        if(!loragotchiDXHome.fishid) loragotchiDXHome.fishid=0x4C460001UL;
      }
      loragotchiDXHome.txid=(uint32_t)(millis() ^ loragotchiDXHome.fishid);
      loragotchiDXHome.destinationIndex=loRaFishDXContactIndex;
      strncpy(loragotchiDXHome.destinationName,c.name,sizeof(loragotchiDXHome.destinationName)-1);
      char msg[112]; snprintf(msg,sizeof(msg),"LFG1|REQ|%lu|%lu|%u|%s",
        (unsigned long)loragotchiDXHome.txid,(unsigned long)loragotchiDXHome.fishid,
        (unsigned)fish.evolution,loRaFishDisplayName());
      int r=the_mesh.sendMessage(c,the_mesh.getRTCClock()->getCurrentTime(),0,msg,ack,to);
      loragotchiDXHome.pending=(r>=0);
      if(loragotchiDXHome.pending) { loRaFishDXRequestStarted=millis(); loragotchiDXSave(); }
      Serial.printf("[LFG1 M2] REQ TX result=%d to='%s'\n",r,c.name);
      showAlert(r>=0?"DX request sent":"DX send failed",800);
    }
    _next_refresh=0; return 0;
  }

  if (loRaFishPage == 5) {
    if (loRaFishOption == 0) {
      loRaFishTimeoutSetting = (loRaFishTimeoutSetting + 1) % 4;
      saveLoRaFishSettings();
      showAlert("Timeout changed", 700);

    } else if (loRaFishOption == 1) {
      resetLoRaFishPet();
      loRaFishPage = 0;
      loRaFishOption = 0;
      loRaFishHatchMelodyPlayed = false;
      showAlert("New egg!", 900);
      _next_refresh = 0;

    } else if (loRaFishOption == 2) {
      // Options consumes short clicks, so explicitly open the RF monitor here.
      loRaFishPage = 6;
      loRaFishOption = 0;
      _next_refresh = 0;
    }

    // Consume the event: don't let MeshCore act on the same long press.
    return 0;
  }

  // Protected design: long press outside Options cannot reset the pet.
  return 0;
#endif

  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double-click triggered");
#ifdef LORAFISH_UI
  if (loRaFishPage == 4) {
    loRaFishPage = 5;  // DX Visit -> Options
    loRaFishOption = 0;
    _next_refresh = 0;
    return 0;
  }
  if (loRaFishPage == 5) {
    loRaFishPage = 0;  // Options -> Aquarium
    loRaFishOption = 0;
    _next_refresh = 0;
    return 0;
  }
#endif
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  checkDisplayOn(c);
  toggleBuzzer();
  c = 0;
  return c;
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return !strcmp(_sensors->getSettingValue(i), "1");
      }
    }
  }
  return false;
}

void UITask::toggleGPS() {
    if (_sensors != NULL) {
    // toggle GPS on/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          _node_prefs->gps_enabled = 0;
          notify(UIEventType::ack);
        } else {
          _sensors->setSettingValue("gps", "1");
          _node_prefs->gps_enabled = 1;
          notify(UIEventType::ack);
        }
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
        _next_refresh = 0;
        break;
      }
    }
  }
}

void UITask::playHatchMelody() {
#ifdef PIN_BUZZER
  buzzer.play("hatch:d=16,o=6,b=150:c,e,g,c7");
#endif
}

void UITask::playFeedPip() {
#ifdef PIN_BUZZER
  buzzer.play("feed:d=32,o=8,b=120:c");
#endif
}

void UITask::playCleanMelody() {
#ifdef PIN_BUZZER
  // Short cheerful ascending clean-up jingle.
  buzzer.play("clean:d=16,o=7,b=180:c,e,g,c8");
#endif
}

void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;  // trigger refresh
  #endif
}
