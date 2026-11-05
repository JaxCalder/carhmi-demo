# carhmi API Reference

## Usage

```bash
carhmi [<view>] [<display>] [<music directory>]
```

| Argument | Meaning |
|----------|---------|
| `<view>` | The `.dmv` to show (default: found - `/eviews/carhmi_view/carhmi.dmv`, else `views/carhmi_view/carhmi.dmv` on any card in `/mnt`) |
| `<display>` | Name of the display (default: the first one dmview serves) |
| `<music directory>` | The MP3 files of the player (default: `music` on the card the view is on, else the first `/mnt/<card>/music`) |

carhmi waits for the view (up to 30 s - it may be on a card mounted after the
start) and for the display (up to 10 s), claims it for the view
(`libdmview_claim()`) and runs until it is stopped, then releases it: the
display's dmview returns to its own view.

Exit code: 0, `-ENOENT` (no view), `-ENODEV` (no display), `-ENOMEM`, or the
error of `libdmview_claim()`.

## The player

The MP3 files of the music directory (sorted by name, as many as there are)
are the player's queue: a list (dmlist) with a block of each file's name;
their ID3 tags - the title and the artist (the file's name when there is
none), the length - are read when a track is shown or played, and by a
thread of the player ahead of the shown window (after it first, then before
it, when nothing else wants the CPU), so a window the list moves to is shown
at once. A thread of
carhmi decodes the current one (dmmp3) and plays it through the audio
service (libdmaudio, on the first audio device); at its end the next one
starts.

The view shows twenty rows of the queue: a window over all the tracks, from
`first`. When the list is scrolled to its end (its start), the view asks for
more - the event `scroll`, +10 (-10) -, carhmi moves the window, and the view
scrolls its rows back by as many: what is seen stays where it is, and the
next tracks are below it. The rows and `first` are set together. While
the window moves, and half a second after, a row's click is not taken: a
drag the move cut short does not end as a tap on a row.

When carhmi moves the window itself - at the start, and when the playing
track is out of it (the next one after the window's end, one from `next` /
`prev`) - the track has ten rows before it in the window (as far as there
are tracks), and the view, seeing `first` change without having asked,
scrolls the track's row (`track`) to the top: the tracks before it are
there at once, to be scrolled up to. A bar at the list's right edge shows
where the first row seen is among all the tracks (`first`, how far the list
is scrolled, `total`). Dragged, it takes the list to the track its thumb
stands at: a track in the window is scrolled to, one out of it is asked for
(the event `jump`, the track), and carhmi moves the window there as it does
itself - the view scrolls that track to the top.

Where the music was - the track (its file's name), the second in it, whether
it played - is kept in `.playing` next to the music
(`/mnt/<card>/music/.playing`, `playstate.c`): written at once when the track
or the playing changes, and every 15 seconds while it plays. At the next start
the same track goes on from there (paused, when it was), shown at the top of
the list with the tracks before it above; a file that is not there any more:
the first track.

## The covers

The front cover of a track's ID3 tag (`dmmp3_find_picture()`; a baseline
JPEG or a PNG) is shown where the view's own image is - the playing track's
(`cover`) and the rows' (`rowN.cover`). The view (`data-dmod-src`) says what
to make of it: the sizes it draws a cover at, with their cuts, round corners
and blur (`cover.sizes`, `row0.cover.sizes`). carhmi (`covers.c`) decodes the
picture with dmimg - at the decoder's smallest scale, then averaged down as
far as the sizes let it - and makes each size with libtodmvi, into
`.covers/<key>-<size>.dmvi` next to the music; the key is a hash of the
picture, so the tracks of an album share their files. A cover made once is
read from the card the next time.

Two threads make them: one what is shown - the playing track, the rows of
the window - at the views' priority, as soon as it is asked for; the other
the rest of the tracks, one after another, only when nothing else wants the
CPU. `covers.done` of `covers.total` tracks are done; the view shows it
("Okładki 34/212") until they all are. A track without a picture, or with
one the decoders do not read (a progressive JPEG), keeps the view's image.

The decoders are loaded by name (`dmimg_jpeg`, `dmimg_png`) - they are not in
carhmi's dependencies: install them with it.

## The clock

The clock of the top bar is the RTC's: carhmi reads dmutc (`dmutc_get()`)
once a second and shows it as Polish time - UTC+1, UTC+2 from the last
Sunday of March to the last Sunday of October at 01:00 UTC (the EU rule;
the RTC keeps UTC). Without an RTC dmutc bound - no `dmutc@<rtc>` service
running, or its calendar not set - the clock is `--:--`.

## The volume

The loudspeaker in the top bar opens a panel under it: the output, mute, - / + (ten
per cent a tap), the level; it closes three seconds after the last touch. The
view says what was done and carhmi does it (`volume.c`): the master volume of
the first audio device (`libdmaudio_device_set_volume()` and
`_device_set_mute()`), so it follows the music at once, track after track.
A new level turns the sound on again. The device may not be served yet when
carhmi starts: it is set as soon as it is.

The level is kept in `.volume` next to the music (`/mnt/<card>/music/.volume`),
written two seconds after the last change, and set again at the next start
(60 when there is none). Muting is not kept.

The output button switches the device between the speaker and the headphones
- the jack is also the line out for an amplifier (`libdmaudio_device_set_output()`,
dmaudio 0.3; the board has no plug detection, so it is a tap). It shows what
the device says it plays through: an output the codec refuses goes back. The
choice is kept in `.output` next to the music (`speaker` / `headphone`) and set
again at the next start; without the file the device keeps the output its
service was started with (dmod-boot's `.dmd`: `speaker/dmaudio@.ini` or the
headphone one).

## The sounds of the car

The last page of the side bar has two buttons, **SILNIK** (the engine) and **SYRENA** (the police siren). A tap
switches the sound on or off; the button shows what is heard. The sounds are made, not played from files
(`sfx_synth.c`: arithmetic only, mono, 22.05 kHz - no samples, no libm; the pitch follows the revs: the firing
of the cylinders is the clock), and written by a thread of its own
(`sfx.c`) into a stream of libdmaudio, which the device's mixer adds to the music.

- **The engine** is a V8 with a sports exhaust, started with carhmi: the starter cranks it (1.4 s), it catches with
  a bark (up to 2700 rpm, a few misfires), falls to the idle (900 rpm) and idles. **The music starts when that
  is over** (3.3 s; if it does not end within 6 s - there is no audio device - the music starts anyway). Switched
  off, the engine dies down in a second; switched on again, it starts again.
- **While it idles it is revved now and then**, every 7 - 16 seconds, one of three things: a blip of the throttle
  (to 3800 rpm and back, the exhaust pops), a double blip, or a pull through three gears (to 6300 rpm, the
  ignition cut and a bang at every shift, the revs fall back for the next gear, at the end the throttle is
  lifted off and the exhaust crackles). `sfx_synth_maneuver()` starts one by hand.
- **The fuel pump** combines a 105 Hz motor hum with filtered noise for flowing fuel. It plays only while
  the hose is connected and the tank is filling, fading out when filling ends or is interrupted.
- **The siren** is a wail: 640 - 1440 Hz and back in 3.4 s. It fades in and out in 50 ms.

Nothing is kept: the engine is on at every start, the siren off. Without the audio device the buttons still
switch, and there is nothing to hear.

## Fuel and refuelling

The simulated tank starts full and lasts 15 minutes of cumulative engine running
time (`fuel.c`); stopping the engine pauses consumption. At empty, the engine
stops and cannot restart until fuel is added. A restart attempt shows the red
empty-tank appearance. Restarting carhmi itself resets the tank to full.

The fuel percentage on the top bar and the dashboard gauge open the tank beside
the sidebar. The filler cycles through closed cap, open cap, connected hose,
disconnected hose, closed cap. Filling takes 20 seconds from empty; it stops at
full or when the hose is removed. Clicking the filler removes the hose first,
then closes the cap on the following click. Closing the panel or selecting a
sidebar page also disconnects and closes it. Fuel already added remains.

A running engine blocks opening/connecting the filler. Each blocked attempt
starts four 300 ms on/off flashes of a crossed-out engine beside the filler,
then the icon disappears. The application computes `fuel.warning` from elapsed
uptime, so redraws do not restart the animation. Another attempt restarts it;
closing the panel or stopping the engine cancels it. Starting the engine with
the hose still connected is also blocked. The browser adapter mirrors the model.

## The channel

carhmi and the view talk through the claim's channel (libdmview 0.10;
the scrolling needs dmview's SCROLLED, 0.12.1):

| Property | Set by | What it is |
|----------|--------|------------|
| `total` | carhmi | The tracks of the queue |
| `first`, `count` | carhmi | The window's first track, its rows that have one |
| `track` | carhmi | The current track's row in the window (-1: not in it) |
| `playing` | carhmi | 1 while it plays |
| `title`, `artist` | carhmi | Of the current track |
| `position`, `duration` | carhmi | Seconds |
| `progress` | carhmi | 0..1000, the width of the progress bar (`data-dmod-width`) |
| `rowN.title`, `.artist`, `.time` | carhmi | The twenty rows of the window |
| `clock` | carhmi | `HH:MM`, local time (`--:--`: not known) |
| `volume` | carhmi | 0..100, the master volume (`data-dmod-width`, scale 100) |
| `muted` | carhmi | 1 while the sound is off |
| `headphones` | carhmi | 1: the device plays through the headphones (line out), 0: the speaker |
| `fuel.level` | carhmi | 0..1000 of the tank; native height/Y binding fills it from the bottom |
| `fuel.stage` | carhmi | 0: capped, 1: opened ready for hose, 2: pumping, 3: full with hose, 4: hose removed, ready to close |
| `fuel.notice` | carhmi | 0: none, 1: empty tank, 2: running engine blocks filling, 3: connected hose blocks starting |
| `fuel.warning` | carhmi | 1 during each visible flash of the temporary engine warning, otherwise 0 |
| `engine`, `siren` | carhmi | 1: the sound is wanted (the engine starts or idles, the siren wails), 0: not |
| `cover`, `rowN.cover` | carhmi | The image of the playing track, of a row: `<music>/.covers/<key>` (the view adds a size's end), else the view's own |
| `cover.sizes`, `row0.cover.sizes` | the view | The ends of the files' names of the sizes it shows a cover at (todmvs) |
| `covers.done`, `covers.total` | carhmi | Tracks whose covers are done, of all |

| Event | Value | What carhmi does |
|-------|-------|------------------|
| `toggle` | - | Play / pause (the first track when none was played) |
| `next`, `prev` | - | The next / previous track, around (the window follows it) |
| `select` | row | Plays the track of that row of the window |
| `scroll` | +n / -n | Moves the window by n tracks (as far as there are) |
| `jump` | track | Moves the window to the track, ten rows before it (as far as there are) |
| `volume` | 0..100 | Sets the volume (and turns the sound on) |
| `mute` | - | Mutes, or turns the sound on |
| `output` | - | Switches between the speaker and the headphones |
| `fuel.open` | - | Opens the tank panel with the cap closed |
| `fuel.close` | - | Disconnects the hose, closes the cap and panel, cancels the flashing warning |
| `fuel.filler` | - | Opens cap / inserts hose / removes hose / closes cap; running engine blocks opening and filling |
| `engine` | - | Starts or stops the engine; an empty tank or connected hose blocks starting |
| `siren` | - | Switches the siren |
