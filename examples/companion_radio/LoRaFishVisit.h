#pragma once
#include "LoRaFishTiming.h"
#include <string.h>

// RAM-only control state. The existing home record remains byte-for-byte intact.
// Unconfirmed never means HOME; home.committing/away retain the exact peer/IDs.
struct LoRaFishVisit {
  enum Phase : uint8_t { Idle, Request, Commit, Recall, Unconfirmed, NoReply };
  Phase phase = Idle;
  uint8_t attempts = 0;
  uint32_t due = 0, deadline = 0, wait = 0;
  bool queueBlocked = false;
  char request[112] = {};

  void begin(Phase next, uint32_t now) {
    phase = next;
    attempts = 0;
    due = now;
    deadline = now + LoRaFishTiming::phaseMs;
    queueBlocked = false;
  }
  bool running() const { return phase == Request || phase == Commit || phase == Recall; }
  bool ready(uint32_t now) const { return running() && LoRaFishTiming::reached(now, due); }
  bool exhausted(uint32_t now) const {
    return running() && (LoRaFishTiming::reached(now, deadline) ||
        (!queueBlocked && attempts >= LoRaFishTiming::maxAttempts && ready(now)));
  }
  void sent(uint32_t now, uint32_t estimate, uint32_t random, bool queued) {
    if (queued) ++attempts;
    wait = LoRaFishTiming::retryMs(queued ? estimate : 0, attempts, random);
    due = now + wait;
  }
  void stop() { phase = Idle; }
};

struct LoRaFishReservation {
  uint32_t due = 0;
  bool expired = false;
  void start(uint32_t now) { due = now + LoRaFishTiming::reserveMs; expired = false; }
};
