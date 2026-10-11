# carhmi API Reference

## Usage

```bash
carhmi [<view>] [<display>] [<music directory>]
```

| Argument | Meaning |
|----------|---------|
| `<view>` | The `.dmv` to show (default `/eviews/carhmi_view/carhmi.dmv`; the service unit gives the one on the SD card) |
| `<display>` | Name of the display (default: the first one dmview serves) |
| `<music directory>` | The MP3 files of the player (default: `music` on the card the view is on, else the first `/mnt/<card>/music`) |

carhmi waits for the view (up to 30 s - it may be on a card mounted after the
start) and for the display (up to 10 s), claims it for the view
(`libdmview_claim()`) and runs until it is stopped, then releases it: the
display's dmview returns to its own view.

Exit code: 0, `-ENOENT` (no view), `-ENODEV` (no display), `-ENOMEM`, or the
error of `libdmview_claim()`.

## The player

The MP3 files of the music directory (sorted by name, up to 200) are the
player's queue: their ID3 tags give the title and the artist (the file's name
when there is none). A thread of carhmi decodes the current one (dmmp3) and
plays it through the audio service (libdmaudio, on the first audio device);
at its end the next one starts.

carhmi and the view talk through the claim's channel (libdmview 0.10):

| Property | Set by | What it is |
|----------|--------|------------|
| `track`, `count` | carhmi | The current track (-1: none) and how many there are |
| `playing` | carhmi | 1 while it plays |
| `title`, `artist` | carhmi | Of the current track |
| `position`, `duration` | carhmi | Seconds |
| `progress` | carhmi | 0..1000, the width of the progress bar (`data-dmod-width`) |
| `rowN.title`, `.artist`, `.time` | carhmi | The ten rows of the queue the view shows |

| Event | Value | What carhmi does |
|-------|-------|------------------|
| `toggle` | - | Play / pause (the first track when none was played) |
| `next`, `prev` | - | The next / previous track, around |
| `select` | row | Plays the track of that row |
