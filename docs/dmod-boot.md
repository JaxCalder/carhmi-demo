# carhmi on dmod-boot

carhmi is a service: dmod-boot starts it at boot, it claims the display for
the car HMI's view, and the display's `dmview` service shows it.

## Two parts

`dmod-boot/flash.dmd` - the flash modules:

```
dmview
dmview service=dmview@.ini      # one dmview per display
dmview rules=dmview.rules
carhmi service=carhmi.ini       # claims the display for the view
dmaudio service=dmaudio@.ini    # the player's audio: one dmaudio per audio device
dmaudio rules=dmaudio.rules
```

`dmod-boot/sdcard.dmd` - the view (about 320 kB: it does not fit in a
microcontroller's flash):

```
carhmi_view
```

```bash
cmake -S . -B build -DBOARD=stm32f746g-disco \
    -DDMBOOT_EXTRA_FLASH_DMD_FILES=<carhmi-demo>/dmod-boot/flash.dmd \
    -DDMBOOT_EXTRA_SDCARD_DMD_FILES=<carhmi-demo>/dmod-boot/sdcard.dmd
```

The board needs a display (and a touch panel for the buttons) in a
`friends_group`, the SD card driver, and for the player an audio device
(dmdrvi 2.7 audio, e.g. the WM8994 codec) - `stm32f746g-disco` has them all;
dmod-boot's own configuration of it already includes carhmi (see its README),
so these files are for other boards.

## Selecting the screen

The same `carhmi_view` package contains both resolutions. In a board's sdcard
manifest use:

```dmd
carhmi_view @screen 320x240
```

Use 480x272 for STM32F746G-DISCO. dmod-boot can also pass a board-wide default
with `-DDMBOOT_SCREEN=320x240`; an entry's `@screen` overrides that default.
Selection applies to flash, external-flash and SD-card installations. The
installer must support `--screen` and the new DMR attributes. All variants are
in one ZIP, but only the selected bundle is installed. The existing carhmi
service and its view path stay the same for both resolutions.

## The view on the card

dmf-get installs the view to `build/sdcard/views/carhmi_view/`. Copy it to a
FAT card, or send it to a running board over the console:

```bash
cmake --build . --target upload-files
```

It is then `views/carhmi_view/carhmi.dmv` on the card, wherever automount
mounts it - `/mnt/<label>`, or `/mnt/dmsdio0_0p1` (the node) for a card
without a label: carhmi looks at every card in `/mnt`. The service waits up
to 30 seconds for the view to appear, so it starts after the card is mounted.
A view on another path: give it to the unit (`args=` of
[configs/carhmi.ini](../configs/carhmi.ini)), or run
`carhmi <view> [<display>]` by hand.

## Music

The player plays the MP3 files of `music/` on the card - next to `views/`
(e.g. `/mnt/dmsdio0_0p1/music`). Put them there when the card is in a PC, or
send them over the console - to the card the script finds on the board:

```bash
python3 <dmod-boot>/scripts/ymodem_send.py --dest music song.mp3
```

Give FAT time to write it out before a reset (a few seconds after the
transfer). carhmi reads the directory when it starts: restart it
(`service restart carhmi`) or the board to see new files.

## A view in the firmware

On a board with more flash, add `carhmi_view` to a *flash* `.dmd` instead: its
views are embedded and mounted at `/eviews/carhmi_view/`, where carhmi looks
first.

## Fuel interaction check on the board

Copy `test_navigation_input.dmfc` from the CarHMI build to the card's `bin/`.
With the matching view installed, run:

```text
test_navigation_input /mnt/DMODOS/views/carhmi_view/carhmi.dmv --pump
```

Use the actual card mount path. The test exercises the navigation and fuel
controls with synthetic contacts in an off-screen view, simulates consumption
and filling without waiting 15 minutes, then plays two seconds of the pump
through the board's real audio stream. Omit `--pump` for a silent check.
