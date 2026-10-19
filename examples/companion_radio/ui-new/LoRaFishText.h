#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <helpers/UTF8Helpers.h>

// Display-only fallback for the small built-in ASCII fonts. Keep raw names in
// contacts/adverts/protocol messages; never feed UTF-8 bytes directly to a panel.
namespace LoRaFishText {

inline uint32_t next(const char* text, size_t capacity, size_t& pos) {
  const uint8_t first = (uint8_t)text[pos];
  size_t length = first < 0x80 ? 1 : first >= 0xC2 && first <= 0xDF ? 2 :
      first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
  if (!length || length > capacity - pos) { ++pos; return 0xFFFD; }
  // Bound and terminate the single code point before using MeshCore's validator.
  char sequence[5] = {};
  for (size_t i = 0; i < length; ++i) {
    if (!text[pos + i]) { ++pos; return 0xFFFD; }
    sequence[i] = text[pos + i];
  }
  if (mesh::validUtf8PrefixLength(sequence, length) != length) {
    ++pos;
    return 0xFFFD;
  }
  uint32_t code = first & (length == 1 ? 0x7F : length == 2 ? 0x1F : length == 3 ? 0x0F : 0x07);
  for (size_t i = 1; i < length; ++i) code = (code << 6) | ((uint8_t)sequence[i] & 0x3F);
  pos += length;
  return code;
}

inline bool isFish(uint32_t code) {
  // Unicode fish, tropical fish, blowfish and shark. Not whales/dolphins,
  // fishing poles, fish cakes, or other marine animals: this is an opt-in marker.
  return code == 0x1F41F || code == 0x1F420 || code == 0x1F421 || code == 0x1F988;
}

inline bool hasFish(const char* text, size_t capacity) {
  if (!text) return false;
  for (size_t pos = 0; pos < capacity && text[pos]; ) {
    if (isFish(next(text, capacity, pos))) return true;
  }
  return false;
}

inline void display(char* dest, size_t size, const char* text, size_t capacity) {
  if (!dest || !size) return;
  size_t out = 0;
  if (text) for (size_t pos = 0; pos < capacity && text[pos]; ) {
    const uint32_t code = next(text, capacity, pos);
    // Presentation selectors, joiners and emoji modifiers have no separate glyph.
    if (code == 0x200D || code == 0xFE0E || code == 0xFE0F ||
        (code >= 0x1F3FB && code <= 0x1F3FF) || (code >= 0xE0020 && code <= 0xE007F)) continue;
    if (isFish(code)) {
      if (out + 3 >= size) break;
      memcpy(dest + out, "><>", 3);
      out += 3;
    } else {
      if (out + 1 >= size) break;
      dest[out++] = code >= 32 && code <= 126 ? (char)code : ' ';
    }
  }
  dest[out] = 0;
}

// Preserve valid raw UTF-8 in the existing bounded DX name field, without
// cutting a multibyte character at the end of the wire-format name limit.
inline void rawPrefix(char* dest, size_t size, const char* text, size_t capacity) {
  if (!dest || !size) return;
  size_t copied = 0;
  if (text) for (size_t pos = 0; pos < capacity && text[pos]; ) {
    const size_t start = pos;
    if (next(text, capacity, pos) == 0xFFFD || pos >= size) break;
    memcpy(dest + start, text + start, pos - start);
    copied = pos;
  }
  dest[copied] = 0;
}

}  // namespace LoRaFishText
