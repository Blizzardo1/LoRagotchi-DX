## Getting Started with LoRaGotchi DX

Thank you for downloading and trying our firmware! LoRaGotchi DX is a virtual pet running as a MeshCore BLE companion. The firmware uses the device's LoRa radio and display for the aquarium, and can exchange visiting fish with compatible LoRaGotchi DX peers over the mesh.

It's actually built straight on the stock meshcore (1.17.1 at the time of writing) firmware with the idea that, you can flash to LoRagotchi-DX without losing your settings, channels, contacts etc. We've even flashed it via the meshcore.io flasher.

For supported boards and PlatformIO build commands, see the [README](README.md#supported-loragotchi-targets-in-this-tree). This guide describes the LoRaFish companion UI and behavior.

## First Use

1. Flash the LoRaGotchi DX build for your board. The Heltec T096 and T114 builds produce a UF2 file; see its README instructions. Other target build commands are listed there.
2. Start the device and connect to it from the MeshCore app as a BLE companion. Use the PIN shown on the device's pairing screen when the app requests one.
3. Set the device clock from the app. LoRaGotchi uses MeshCore's clock for the displayed local time and scheduled sleep. If the clock has not been set to a valid date, the UI falls back to build time plus uptime; this fallback is not a substitute for setting the clock.
4. Use **Options → Schedule & time** to set your local UTC offset and sleep/wake hours.

Pet age, RF-event count, happiness, and other pet state are saved across restarts. Display and schedule settings are also saved. DX travel is temporary: after a reboot, the local fish returns home and any visiting-fish slot is cleared.

## Button Controls

The T096 and T114 use a single user button. Hold it for about one second for a long-press. A click, double-click, long-press, and triple-click have different meanings depending on the screen.

| Gesture | General action |
| --- | --- |
| Short click | Advance to the next main screen, or change/select an item in a menu. |
| Long-press | Open the selected Options item or perform the current screen's action. |
| Double-click | Move to the next field in Schedule or Timeout; return to a parent menu on supported submenus. |
| Triple-click | Toggle global buzzer quiet mode when the board has a buzzer. |

During the first eight seconds after startup, long-press is reserved for MeshCore's CLI rescue gesture. After that, the LoRaGotchi menu actions are available.

When the display has turned off, the first click wakes it and does not also advance the screen. On the aquarium, long-press starts cleaning if waste is present. It does not open Options from the aquarium.

The main screens advance in this order:

1. Aquarium
2. Stats
3. Clock
4. Phone pairing
5. DX Visit
6. Options
7. RF Monitor
8. GNSS

Short-click on **Options** moves the selection. Long-press opens the selected item. Short-click cycles a value within a value editor; double-click generally advances to the next field; long-press returns to the parent menu.

## Options and Display

The main **Options** menu contains:

- **Display**: screen timeout, brightness, firmware theme, and fish color on color displays.
- **Reset pet**: start a new egg and reset pet progress. Display and schedule preferences are kept.
- **RF monitor**: view recent received signal information and activity.
- **GNSS monitor**: inspect GNSS status; long-press toggles GNSS where supported.
- **Schedule & time**: configure sleep, wake, and UTC offset.
- **Sound**: configure buzzer volume and pet/message sounds. This item appears only on buzzer-equipped LoRaFish builds.

### Display submenu

Open **Options → Display**. Short-click selects an item and long-press opens it. Double-click returns to Options.

- **Screen timeout** has independent **Fish Awake** and **Fish Asleep** settings. Each can be set to 15 seconds, 30 seconds, 1 minute, 2 minutes, 3 minutes, or Always On. The schedule determines which timeout is active.
- **Always-On-Dim** selects when an always-on screen dims after inactivity: 15, 30, or 60 seconds. It applies when the active awake/asleep timeout is Always On. A button press restores the selected brightness.
- **Brightness** cycles through 25%, 50%, 75%, and 100%. The default is 100%. Brightness is controlled by OLED contrast on the V4 and backlight level on color TFT targets.
- **Firmware theme** cycles the available themes. Color TFTs have Midnight, Paper, Lagoon, and Sunset. Monochrome OLEDs use the dark and paper themes.
- **Fish color** cycles Coral, Tangerine, Gold, Mint, Lagoon, and Orchid. This option is available only on color displays. A monochrome screen renders fish in its foreground color.

### Sound menu

On buzzer-equipped LoRaGotchi targets, open **Options → Sound**. Short-click changes the selected setting and previews non-Off choices; double-click moves to the next row; long-press returns to Options. The settings are:

- **Volume**: Off, 25%, 50%, 75%, or 100%. Volume changes the buzzer's PWM duty; triple-click remains a separate global quiet-mode toggle.
- **Wake alarm**: Off, Chirp, Rising, or Alarm. The selected original short motif plays when the scheduled sleep state changes to awake.
- **Sleep cue**: Off, Drift, or Soft bell. The selected motif plays when the fish enters scheduled sleep.
- **Message tone**: Off, Ping, Double, or Arpeggio for incoming messages.

The default is 100% volume, wake alarm Off, sleep cue Off, and Ping for incoming messages. The short motifs are original compositions rather than copied Nokia, Tamagotchi, or other device ringtones. Sound options are hidden on boards without a buzzer; T096 and T114 builds do not currently define buzzer hardware.

## Pet Behavior

### Growth and age

A new pet starts as an egg. It hatches after five minutes. The fish then progresses according to age:

- **Fry**: from hatching until one hour old.
- **Young**: one hour to six hours.
- **Adult**: six hours to three days.
- **Elder**: three days until the end of its lifespan.
- **Memorial**: shown at seven days; use **Options → Reset pet** to start over.

The age clock continues while the display is off and while the fish sleeps. The aquarium animation is only a presentation; turning the display off does not pause the pet.

### Mesh activity, meals, and hygiene

MeshCore interactions such as received messages, acknowledgments, and new contacts contribute to the RF-event counter. Each group of ten events earns a meal while the fish is awake. Meals increase happiness and schedule waste to appear 30 to 90 seconds later. Happiness is capped at 100. The buzzer, when fitted and enabled, provides hatch, feeding, and cleaning sounds.

Waste accumulates up to three pieces. To clean the tank, long-press while on the aquarium screen. Cleaning clears the waste and adds a small happiness bonus. Cleaning is available only when waste is present.

### Sleep schedule

The default schedule is sleep at 21:00 and wake at 07:00. Sleep and wake are based on the device clock plus the manually configured UTC offset. Offset increments are 15 minutes, from UTC-12:00 through UTC+14:00. There is no automatic timezone or daylight-saving detection; update the offset when local rules change. Setting sleep and wake to the same hour keeps the fish awake all day.

During scheduled sleep the fish displays as sleeping. RF events are still counted, but meal thresholds reached while asleep are consumed without feeding, so meals do not accumulate and trigger all at once on waking. A sleeping fish cannot start a DX visit, though a fish already away can still be recalled.

## DX Visits

Open **DX Visit** to choose a compatible LoRaGotchi DX peer from the contact list. Short-click cycles through contacts. Long-press sends a visit request when the local fish is awake. The peers exchange request, acknowledgment, commit, and arrival-confirmation messages. Once accepted, the visitor appears in the other aquarium while the host fish continues its own behavior.

When your fish is away, long-press recalls it home. Double-click on DX Visit opens Options. A sleeping fish cannot begin a new visit. If a visit is interrupted or the device reboots, DX travel state is cleared and the local fish is restored at home.

Color-capable displays show visitor color when the visitor supports it. A monochrome visitor appears in cyan on a color display; monochrome displays use their normal foreground color. A fish returning from a monochrome host briefly animates back into its saved home color on a color display.

## Display and Hardware Notes

The UI screens are Aquarium, Stats, Clock, Phone pairing, DX Visit, Options, RF Monitor, and GNSS. GNSS details depend on the board and whether a receiver is fitted.

Fish color controls and colored visitor rendering require a color display; monochrome OLED targets remain monochrome. Monochrome and Colour tanks know about each other and act & display accordingly

Use the companion BLE connection for MeshCore functions such as messages, contacts, and setting device time. LoRaGotchi DX adds the pet UI and DX visit behavior on top of that companion firmware.
