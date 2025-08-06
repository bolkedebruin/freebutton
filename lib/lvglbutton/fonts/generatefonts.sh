#!/usr/bin/env bash
export LC_ALL=en_US.UTF-8

FONT_SIZES=(14 18 20 24 32 48 72)

# use single quotes
USER_SYMBOLS='°³€$£øØÅåæÆéèêàâïîôöùûüçäöüßÄÖÜë↑↓⏴︎⏵︎⏶︎⏷︎⏸︎⏹︎⏭︎⏮︎⏯︎₂'
LARGE_SYMBOLS='0123456789.,:-/APM°FC'

MDI_URL="https://pictogrammers.github.io/@mdi/font/7.4.47/"
ICON_NAMES=(
    "weather-*"
    "volume-*"
    "exit-to-app"
    "lightbulb-*"
    "lightbulb"
    "blinds"
    "blinds-*"
    "window-shutter"
    "window-shutter-open"
    "fan"
    "fan-speed-*"
    "triangle"
    "triangle-down"
)

# Convert array to space-separated string
ICON_NAMES_STRING=$(IFS=" "; echo "${ICON_NAMES[*]}")

# Fetch the HTML content and get the list of glyph codes using get_mdi_codes.sh
GLYPHS=$(curl -s $MDI_URL | ./get_mdi_codes.sh $ICON_NAMES_STRING)

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

