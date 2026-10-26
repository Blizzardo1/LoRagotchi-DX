# Cardputer merge — test build

Base: user-supplied LoRagotchi-DX-main(4).zip (latest pull requests).
Added from the previously working Cardputer ColorVal display fix:
- boards/m5stack_cardputer.json
- variants/m5stack_cardputer/*
- src/helpers/ui/M5CardputerDisplay.{h,cpp}

All existing base source files, including ui-new/UITask.cpp, remain unchanged.
No T-Deck experimental code has been merged.

Build: pio run -e cardputer_loragotchi

ZIP integrity verified; PlatformIO build and hardware functionality have NOT been independently verified for this newly updated base.
