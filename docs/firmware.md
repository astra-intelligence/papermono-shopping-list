# Firmware

## Hardware

An [M5Stack PaperMono](https://docs.m5stack.com/en/core/PaperMono): ESP32-S3R8
(16 MB flash, 8 MB octal PSRAM), a 3.97" 480×800 SSD1677 e-paper panel with an
FT6336G capacitive touch layer, an M5PM1 power-management IC and an M5IOE1 IO
expander. It's been tested on the **C153**. The C153-LITE has the same core
hardware minus NFC and LoRa, which this firmware doesn't use, so it should work
but is untested.

[hardware.md](hardware.md) has the pin map, I²C addresses, bring-up sequence and
the display, touch and power details.

## Configure

Wi-Fi and the server address are compiled in:

```sh
cd firmware
cp src/secrets.example.h src/secrets.h
$EDITOR src/secrets.h
```

```c
#define SHOPPING_LIST_WIFI_SSID "your-wifi-ssid"
#define SHOPPING_LIST_WIFI_PASSWORD "your-wifi-password"
#define SHOPPING_LIST_SERVER_URL "http://192.168.1.50:8000"   // no trailing slash
```

`secrets.h` is git-ignored, and the build fails with a clear error if it's
missing. Set it correctly before flashing: the ESP32 Wi-Fi stack saves whatever
credentials it's given to flash, so a build with placeholder values would
overwrite credentials a previous firmware stored.

Other tunables (sync interval, timeouts, the partial-refresh limit) are in
`src/config.h`.

## Build

With [PlatformIO](https://platformio.org/) (CLI or the VS Code extension):

```sh
pio run
```

The first build downloads the ESP32 toolchain and libraries, which takes a few
minutes.

## Back up the device first

Before flashing anything onto a PaperMono for the first time, save a full copy of
its 16 MB flash. It holds that unit's RF calibration data, and it's the only way
back to the factory firmware.

1. Put the device in download mode: hold the power button for about 2 seconds,
   until the small red LED blinks.
2. Read the whole flash (`esptool` v5 syntax; v4 uses `read_flash`):

   ```sh
   pip install esptool
   esptool --chip esp32s3 --port /dev/ttyACM0 read-flash 0 0x1000000 papermono-backup.bin
   ```

   On macOS the port is `/dev/cu.usbmodem*`; on Windows it's `COMx`.

To restore it later: `esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0 papermono-backup.bin`.

Don't run `erase-flash` unless you have that backup.

## Flash

### Over USB with PlatformIO

```sh
pio run -t upload
pio device monitor        # serial log at 115200 baud
```

If the upload can't connect, put the device in download mode (see above) and try
again.

### From a browser

If the machine the device is plugged into doesn't have PlatformIO, build a single
merged image and flash it from the server's own web flasher (Chrome or Edge, over
WebSerial - see [server.md](server.md)):

```sh
pio run
tools/make_factory_image.sh --catalog ../server/src/shopping_list/flasher/firmware/catalog.json
```

This copies `papermono-shopping-list.factory.bin` next to that catalog and
registers it, creating `catalog.json` if it doesn't exist yet. Both the image
and `catalog.json` are git-ignored, so neither gets committed. Then open
`http://<server-host>:8000/flash/`, connect, and pick it from the catalog - or
skip the catalog step entirely and flash any local `.bin` file by picking it
directly in the page.

## Using it

| Do this | What happens |
|---------|--------------|
| Tap an item | Tick it off, or untick it. Syncs straight away if Wi-Fi is reachable. |
| Tap the quantity on the right of an item's row | Opens a screen to set or change its quantity/unit (e.g. `x3` or `500 g`). Tapping **UNIT** there opens a popup listing all 8 options; tap one to pick it. Shows a muted **qty** placeholder when none is set. |
| Tap **+ ADD ITEM** | Opens the keyboard. As you type, up to three suggestions from past items appear above it; tap one to use it, including its aisle. **SEND** opens the quantity screen for the new item; **CANCEL** there abandons the add. |
| Swipe right, or tap above the keyboard | Closes the keyboard without adding anything. |
| Swipe up or down, or press the side buttons | Page through the list. |
| Tap **SETTINGS** in the add bar (or the header bar, as a shortcut) | Opens settings: frontlight level, **SYNC NOW**, and status (Wi-Fi network, last sync result, battery). Opens with a quick partial update rather than a full-panel flash. |
| Short press the power button | Power off. |

The status LED blinks green after a successful sync and red after a failed one.
The header shows how long ago the last successful sync was. The device syncs
periodically (every hour) and also opportunistically on any tap if the last
sync is more than 5 minutes old, so actively using it keeps the list fresh
without needing a fast fixed interval running in the background the rest of
the time.

Items added while the server can't be reached show in grey under **PENDING SYNC**
and can't be ticked until they've synced. Everything on screen and every queued
edit survives a reboot.

## Power

Wi-Fi is on only during a sync, and the frontlight is off unless you turn it on.
When the battery drops to 3.45 V (resting, not charging), the device draws a
"Battery Low" screen and powers off. E-paper keeps showing the last image with no
power.

Battery life hasn't been measured yet. The main loop polls touch every 20 ms and
doesn't use light sleep between syncs, so expect days, not weeks.

## Troubleshooting

**It never syncs.** Open settings (tap the header). If **LAST SYNC** says
**FAILED**, check that **WI-FI** shows the network you expect, that the server URL
in `secrets.h` is reachable from that network (try `curl <url>/api/health` from
another device on it), and look at the serial log (`pio device monitor`) for the
HTTP status or Wi-Fi timeout.

**It seems frozen for a few seconds after a tap.** A sync is running, and syncs
block the UI. Out of Wi-Fi range, a sync after a tap gives up after 5 s. If Wi-Fi
connects but the server doesn't answer, each request can wait up to 8 s. See
[known limitations](design-notes.md#known-limitations).

**The screen looks ghosted or washed out.** Any action that does a full refresh
cleans it up, such as closing settings. One also happens automatically after
every 10 partial updates.

**It doesn't respond at all.** Press the power button. If USB power is connected
while shutting down, the power IC may refuse to cut power; the firmware then
falls back to deep sleep, which the power button (or a 60 s timer) wakes it from.

## Panel safety

These rules come from M5Stack's guidance and community experience with this
panel. `src/hal/epd.cpp` enforces the first one.

- Don't run an unbounded string of fast partial refreshes. Do a full refresh at
  least every ~10.
- Don't upload custom SSD1677 waveforms (command `0x32`). A waveform that isn't
  DC-balanced can permanently damage the panel.
- Keep the IP2315 charger IC off the shared I²C bus (M5IOE1 pin 11 low). It can
  lock the bus.
