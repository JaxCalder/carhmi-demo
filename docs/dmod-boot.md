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
`friends_group`, and the SD card driver - `stm32f746g-disco` has them.

## The view on the card

dmf-get installs the view to `build/sdcard/views/carhmi_view/`. Copy it to a
FAT card, or send it to a running board over the console:

```bash
cmake --build . --target upload-files
```

It is then `/mnt/dmsdio0_0p1/views/carhmi_view/carhmi.dmv` (the card's first
partition, mounted by automount) - the path in
[configs/carhmi.ini](../configs/carhmi.ini). The service waits up to 30
seconds for the view to appear, so it starts after the card is mounted. A view
on another path: change `args=` of the unit, or run
`carhmi <view> [<display>]` by hand.

## A view in the firmware

On a board with more flash, add `carhmi_view` to a *flash* `.dmd` instead: its
views are embedded and mounted at `/eviews/carhmi_view/`, so give the unit
`args=/eviews/carhmi_view/carhmi.dmv`.
