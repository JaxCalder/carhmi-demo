# carhmi-demo

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/carhmi-demo/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/carhmi-demo/actions/workflows/ci.yml)

A car dashboard (HMI) for a 480 x 272 display - an example of how easily a
dmodOS application gets a GUI: the interface is **one HTML page**
([assets/car_hmi.html](assets/car_hmi.html)), made with Tailwind CSS and
JavaScript, with no code of its own for the screen.

At build time the page is laid out as a browser does and compiled into a
dmview view: its pages, the speedometer, the player and the climate controls,
animations and touches all run on the device, in dmview - the application
only claims the display for the view.

## What's here

| Module | Kind | What it is |
|--------|------|------------|
| `carhmi` | Application (service) | Claims the display for the view while it runs, plays the MP3 files of the SD card |
| `carhmi_view` | Data | The view: `carhmi.dmv` with its fonts (`.dmvf`) and images (`.dmvi`) |

The view is a package of its own, as it is too big for a microcontroller's
flash: it goes to the SD card.

## Running it on dmod-boot

```bash
cmake -S . -B build -DBOARD=stm32f746g-disco \
    -DDMBOOT_EXTRA_FLASH_DMD_FILES=<carhmi-demo>/dmod-boot/flash.dmd \
    -DDMBOOT_EXTRA_SDCARD_DMD_FILES=<carhmi-demo>/dmod-boot/sdcard.dmd
cd build
cmake --build . --target install-firmware   # the firmware
cmake --build . --target upload-files       # the view, to the SD card over the console
```

See [docs/dmod-boot.md](docs/dmod-boot.md).

## The page

[assets](assets) is the design: `car_hmi.html`, the fonts it uses
(Inter, Phosphor Icons - `assets/fonts`) and the cover images
(`assets/images`), all next to it, so it also opens in a browser.
[docs/view.md](docs/view.md) describes how it becomes a view.

## Building

```bash
cmake -S . -B build -DDMOD_TOOLS_NAME=arch/armv7/cortex-m7
cmake --build build
```

The build downloads `todmvs` (and the converters it loads) with `dmf-get`, so
it needs `dmf-get` and `dmod_loader`. Pass `-DDMOD_DIR=/path/to/local/dmod`
to build against a local dmod checkout.

## Project Structure

```
carhmi-demo/
├── assets/
│   ├── car_hmi.html        # the design
│   ├── fonts/              # Inter, Phosphor Icons (+ their licenses)
│   └── images/             # cover images
├── cmake/carhmi_page.cmake # page -> the view's sources (todmvs)
├── configs/carhmi.ini      # the service unit
├── dmod-boot/              # flash.dmd and sdcard.dmd to add to dmod-boot
├── src/carhmi.c            # the application
├── src/carhmi_view.c       # the data module
├── carhmi.dmr              # package of carhmi
├── carhmi_view.dmr         # package of carhmi_view
├── docs/
└── tests/
```

## License

MIT - see [LICENSE](LICENSE). The fonts and images in `assets` have their own
licenses: Inter (SIL OFL, `assets/fonts/LICENSE-Inter.txt`), Phosphor Icons
(MIT, `assets/fonts/LICENSE-Phosphor.txt`), the photos are from
[Unsplash](https://unsplash.com/license).
