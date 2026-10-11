# carhmi API Reference

## Usage

```bash
carhmi [<view>] [<display>]
```

| Argument | Meaning |
|----------|---------|
| `<view>` | The `.dmv` to show (default `/eviews/carhmi_view/carhmi.dmv`; the service unit gives the one on the SD card) |
| `<display>` | Name of the display (default: the first one dmview serves) |

carhmi waits for the view (up to 30 s - it may be on a card mounted after the
start) and for the display (up to 10 s), claims it for the view
(`libdmview_claim()`) and runs until it is stopped, then releases it: the
display's dmview returns to its own view.

Exit code: 0, `-ENOENT` (no view), `-ENODEV` (no display), `-ENOMEM`, or the
error of `libdmview_claim()`.
