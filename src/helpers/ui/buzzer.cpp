#include "Arduino.h"
#ifdef PIN_BUZZER
#include "buzzer.h"

void genericBuzzer::begin() {
//    Serial.print("DBG: Setting up buzzer on pin ");
//    Serial.println(PIN_BUZZER);
    #ifdef PIN_BUZZER_EN
      pinMode(PIN_BUZZER_EN, OUTPUT);
      digitalWrite(PIN_BUZZER_EN, HIGH);
    #endif

    quiet(false);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW); // need to pull low by default to avoid extreme power draw
}

void genericBuzzer::play(const char *melody) {
    if (isPlaying())   // interrupt existing
    {
        rtttl::stop();
    }

    if (_is_quiet || _volume == 0) return;

    rtttl::begin(PIN_BUZZER,melody);
//    Serial.print("DBG: Playing melody - isQuiet: ");
//    Serial.println(isQuiet());
}

bool genericBuzzer::isPlaying() {
    return rtttl::isPlaying();
}

void genericBuzzer::loop() {
    if (!rtttl::done()) {
      rtttl::play();
      if (!rtttl::done()) applyVolume();
    }
}

void genericBuzzer::setVolume(uint8_t volume) {
    _volume = volume > 4 ? 4 : volume;
    if (_volume == 0 && rtttl::isPlaying()) rtttl::stop();
}

void genericBuzzer::applyVolume() {
#ifdef ESP32
    static const uint16_t duty[] = {0, 128, 256, 384, 512};
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(PIN_BUZZER, duty[_volume]);
#else
    ledcWrite(0, duty[_volume]);
#endif
#endif
}

void genericBuzzer::startup() {
    play(startup_song);
}

void genericBuzzer::shutdown() {
    play(shutdown_song);
}

void genericBuzzer::quiet(bool buzzer_state) {
    _is_quiet = buzzer_state;
#ifdef PIN_BUZZER_EN
    if (_is_quiet) {
      digitalWrite(PIN_BUZZER_EN, LOW);
    } else {
      digitalWrite(PIN_BUZZER_EN, HIGH);
    }
#endif
}

bool genericBuzzer::isQuiet() {
    return _is_quiet;
}

#endif  // ifdef PIN_BUZZER