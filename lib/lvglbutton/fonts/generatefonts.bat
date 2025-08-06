chcp 65001
call lv_font_conv --font Montserrat-Medium.ttf --size 14 --bpp 4 --format lvgl --range 0x20-0x7F --symbols °³€$£øØÅåæÆéèêàâïîôöùûüçäöüßÄÖÜë↑↓ --output mnt_medium_14.c
call lv_font_conv --font Montserrat-Medium.ttf --size 20 --bpp 4 --format lvgl --range 0x20-0x7F --symbols °³€$£øØÅåæÆéèêàâïîôöùûüçäöüßÄÖÜë↑↓ --output mnt_medium_20.c
call lv_font_conv --font Montserrat-Medium.ttf --size 24 --bpp 4 --format lvgl --range 0x20-0x7F --symbols °³€$£øØÅåæÆéèêàâïîôöùûüçäöüßÄÖÜë↑↓ --output mnt_medium_24.c
call lv_font_conv --font Montserrat-Medium.ttf --size 32 --bpp 4 --format lvgl --range 0x20-0x7F --symbols °³€$£øØÅåæÆéèêàâïîôöùûüçäöüßÄÖÜë↑↓ --output mnt_medium_32.c

call lv_font_conv --font Montserrat-Medium.ttf --size 48 --bpp 4 --format lvgl --range 0x20 --symbols 0123456789.,:-/APM°FC --output mnt_medium_48.c
call lv_font_conv --font Montserrat-Medium.ttf --size 72 --bpp 4 --format lvgl --range 0x20 --symbols 0123456789.,:-/APM°FC --output mnt_medium_72.c

call lv_font_conv --font Montserrat-SemiBold.ttf --size 14 --bpp 4 --format lvgl --range 0x20-0x7F --symbols °³€$£øØÅåæÆéèêàâïîôöùûüçäöüßÄÖÜë↑↓ --output mnt_semibold_14.c
call lv_font_conv --font Montserrat-SemiBold.ttf --size 18 --bpp 4 --format lvgl --range 0x20-0x7F --symbols °³€$£øØÅåæÆéèêàâïîôöùûüçäöüßÄÖÜë↑↓ --output mnt_semibold_18.c

rem ←↑→↓⏴︎⏵︎⏶︎⏷︎⏸︎⏹︎⏭︎⏮︎