# From the page to the view

```
assets/car_hmi.html ──(todmvs: dmvs_html + dmvs_js)──► carhmi.dmvs + fonts + images
                                  ──(dmod, DMOD_ASSETS_PATHS)──► carhmi.dmv, .dmvf, .dmvi
```

[cmake/carhmi_page.cmake](../cmake/carhmi_page.cmake) downloads `todmvs` with
`dmf-get` and converts the page when CMake configures (the sources have to
exist when dmod looks for assets). The element `hmi-screen` is the screen; the
page is laid out for `CARHMI_SCREEN` (480x272). dmod's assets step then makes
the view, the fonts (only the characters the view draws) and the images (at
the sizes it draws them).

## What the page may use

The page is a design as a browser shows it; what is converted is what the
tools support (see dmvs_html and dmvs_js):

- HTML, Tailwind CSS (Play CDN script), the cascade, flexbox and grid;
- fonts and icon fonts through `<link rel="stylesheet">` of local files -
  `assets/fonts/*.css` (`@font-face` of TrueType files);
- images by relative paths (`assets/images`) - **baseline** JPEGs or PNGs: the
  JPEG decoder does not read progressive ones. Keep image file names short:
  the names of the converted images (with the size and the cut) are limited;
- JavaScript: what is known when the page is converted is computed then, the
  rest becomes the view's variables and handlers.

What the converter cannot do is reported as warnings while the page is
converted (`todmvs` without `-q`).
