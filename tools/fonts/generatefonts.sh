#!/usr/bin/env bash
#
# DEV-ONLY font regenerator. The compiled LVGL fonts (src/ui/fonts/mnt_*.c)
# are committed to the repo and are the live render path, so a normal build
# does NOT run this script. Run it only when the font set, symbols, or icon
# selection change, then copy the regenerated mnt_*.c into src/ui/fonts/.
#
# Offline by default: the Material Design Icon glyph codepoints are read from
# the committed cache file `mdi_glyphs.txt`. No network access is performed
# when that cache exists. Pass `--refresh` to re-fetch the glyph codes from
# the network (dev machines only) and rewrite the cache.
#
# Requires: lv_font_conv, and (for --refresh) curl + jq.
#
export LC_ALL=en_US.UTF-8

FONT_SIZES=(14 18 20 24 32 48 72)

GLYPH_CACHE="mdi_glyphs.txt"
REFRESH=0
for arg in "$@"; do
    case "$arg" in
        --refresh) REFRESH=1 ;;
        *) echo "Unknown argument: $arg" >&2; echo "Usage: $0 [--refresh]" >&2; exit 1 ;;
    esac
done

# use single quotes - extended for FreeButton
USER_SYMBOLS='°³€$£øØÅåæÆéèêàâïîôöùûüçäöüßÄÖÜë↑↓⏴︎⏵︎⏶︎⏷︎⏸︎⏹︎⏭︎⏮︎⏯︎₂±×÷≠≤≥√∞'
LARGE_SYMBOLS='0123456789.,:-/APM°FC'

MDI_URL="https://pictogrammers.github.io/@mdi/font/7.4.47/"
ICON_NAMES=(
    "weather-*"
    "volume-*"
    "exit-to-app"
    "lightbulb-*"
    "lightbulb"
    "leaf"
    "blinds"
    "blinds-*"
    "window-shutter"
    "window-shutter-open"
    "fan"
    "fan-speed-*"
    "triangle"
    "triangle-down"
    "button"
    "toggle-*"
    "power*"
    "wifi*"
    "bluetooth*"
    "battery*"
    "home*"
    "settings*"
    "menu*"
    "refresh*"
    "play*"
    "pause*"
    "stop*"
    "water*"
    "molecule-co2"
    "air-filter"
    "lightning-bolt"
)

# Convert array to space-separated string
ICON_NAMES_STRING=$(IFS=" "; echo "${ICON_NAMES[*]}")

# Resolve MDI glyph codepoints. Prefer the committed offline cache; only touch
# the network when explicitly refreshing or when no cache is present.
if [ "$REFRESH" -eq 1 ] || [ ! -f "$GLYPH_CACHE" ]; then
    echo "Fetching MDI glyph codes from network..." >&2
    GLYPHS=$(curl -s "$MDI_URL" | ./get_mdi_codes.sh $ICON_NAMES_STRING)
    if [ -z "$GLYPHS" ]; then
        echo "ERROR: failed to fetch MDI glyph codes and no cache available." >&2
        exit 1
    fi
    printf "%s\n" "$GLYPHS" > "$GLYPH_CACHE"
    echo "Wrote glyph cache: $GLYPH_CACHE" >&2
else
    echo "Using offline glyph cache: $GLYPH_CACHE (pass --refresh to update)" >&2
    GLYPHS=$(cat "$GLYPH_CACHE")
fi

MDI_FONT="materialdesignicons-webfont.ttf"

# Prepare the range arguments for each glyph
RANGES=""
for GLYPH in ${GLYPHS//,/ }
do
    RANGES="$RANGES -r $GLYPH"
done

for SIZE in "${FONT_SIZES[@]}" ; do
    lv_font_conv --font Montserrat-Medium.ttf --size ${SIZE} --bpp 4 --format lvgl --range 0x20-0x7F --symbols ${USER_SYMBOLS} --font ${MDI_FONT} ${RANGES} --output mnt_medium_${SIZE}.c
    lv_font_conv  --font Montserrat-Medium.ttf --size ${SIZE} --bpp 4 --format bin --range 0x20-0x7F --symbols ${USER_SYMBOLS} --font ${MDI_FONT} ${RANGES} --output mnt_medium_${SIZE}.bin
done

lv_font_conv --font Montserrat-SemiBold.ttf --size 14 --bpp 4 --format lvgl --range 0x20-0x7F --symbols ${USER_SYMBOLS} --font ${MDI_FONT} ${RANGES} --output mnt_semibold_14.c
lv_font_conv --font Montserrat-SemiBold.ttf --size 18 --bpp 4 --format lvgl --range 0x20-0x7F --symbols ${USER_SYMBOLS} --font ${MDI_FONT} ${RANGES} --output mnt_semibold_18.c

lv_font_conv  --font Montserrat-SemiBold.ttf --size 14 --bpp 4 --format bin --range 0x20-0x7F --symbols ${USER_SYMBOLS} --font ${MDI_FONT} ${RANGES} --output mnt_semibold_14.bin
lv_font_conv --font Montserrat-SemiBold.ttf --size 18 --bpp 4 --format bin --range 0x20-0x7F --symbols ${USER_SYMBOLS} --font ${MDI_FONT} ${RANGES} --output mnt_semibold_18.bin

lv_font_conv --font Montserrat-Medium.ttf  --size 48 --bpp 4 --format lvgl --range 0x20 --symbols ${LARGE_SYMBOLS} --font ${MDI_FONT} ${RANGES} --output mnt_medium_48.c
lv_font_conv --font Montserrat-Medium.ttf --size 72 --bpp 4 --format lvgl --range 0x20 --symbols ${LARGE_SYMBOLS} --font ${MDI_FONT} ${RANGES} --output mnt_medium_72.c

lv_font_conv  --font Montserrat-Medium.ttf --size 48 --bpp 4 --format bin --range 0x20 --symbols ${LARGE_SYMBOLS} --font ${MDI_FONT} ${RANGES} --output mnt_medium_48.bin
lv_font_conv --font Montserrat-Medium.ttf --size 72 --bpp 4 --format bin --range 0x20 --symbols ${LARGE_SYMBOLS} --font ${MDI_FONT} ${RANGES} --output mnt_medium_72.bin

