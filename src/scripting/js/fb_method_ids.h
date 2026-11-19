/*
 * The ids of the methods of the fb object tree. Each method is a closure of
 * js_fb_device_proxy (freebutton_device.c) whose parameter is one of these;
 * device_hardware.cpp makes the closures when a script starts. The ids are
 * not in stored bytecode, so they may be renumbered freely.
 */
#ifndef FB_METHOD_IDS_H
#define FB_METHOD_IDS_H

enum {
    /* led / backLed (_p = strip index) */
    FB_METHOD_LED_SET_COLOR = 0,
    FB_METHOD_LED_ON,
    FB_METHOD_LED_OFF,
    /* <side>.button (_p = button position) */
    FB_METHOD_BTN_ON_CLICK,
    FB_METHOD_BTN_ON_LONG_PRESS,
    FB_METHOD_BTN_ON_RELEASE,
    /* <side>.display (_p = button position) */
    FB_METHOD_DISP_SET_LABEL,
    FB_METHOD_DISP_SET_TOP_LABEL,
    FB_METHOD_DISP_GET_LABEL,
    FB_METHOD_DISP_GET_TOP_LABEL,
    FB_METHOD_DISP_SET_ICON,
    FB_METHOD_DISP_SET_STYLE,    /* setStyle(styles) */
    FB_METHOD_DISP_ADD_STYLE,    /* addStyle(style) */
    FB_METHOD_DISP_REMOVE_STYLE, /* removeStyle(style) */
    /* fb.main.display: the display items (argc / argv are passed on; the
     * first argument is an id or a name where a method takes one) */
    FB_METHOD_DITEM_CREATE,
    FB_METHOD_DITEM_REMOVE,
    FB_METHOD_DITEM_COUNT,
    FB_METHOD_DITEM_GET,
    FB_METHOD_DITEM_GET_ALL,
    FB_METHOD_DITEM_SET_LABEL,
    FB_METHOD_DITEM_SET_VALUE,
    FB_METHOD_DITEM_SET_UNIT,
    FB_METHOD_DITEM_SET_ICON,
    FB_METHOD_DITEM_SET_STYLE,    /* setStyle(idOrName, styles) */
    FB_METHOD_DITEM_ADD_STYLE,    /* addStyle(idOrName, style) */
    FB_METHOD_DITEM_REMOVE_STYLE, /* removeStyle(idOrName, style) */
    FB_METHOD_DITEM_SET_POSITION,
    FB_METHOD_DITEM_SET_PAGE,
    FB_METHOD_DITEM_SHOW_PAGE,
    FB_METHOD_DITEM_GET_SHOWN_PAGE,
    /* fb.main.list.*(nameOrIndex, ...): config "lists" */
    FB_METHOD_LIST_NEXT,
    FB_METHOD_LIST_PREV,
    FB_METHOD_LIST_SELECT,
    FB_METHOD_LIST_GET_SELECTED,
    FB_METHOD_LIST_COUNT,
    FB_METHOD_LIST_SET_ITEMS,
    FB_METHOD_LIST_SET_STYLE, /* setStyle(nameOrIndex, styles) */
    /* fb.main.image.*(nameOrIndex, value): config "images" */
    FB_METHOD_IMAGE_SET_ICON,
    FB_METHOD_IMAGE_SET_SRC,
    /* fb.button(name): the side object of a config.json button name */
    FB_METHOD_BUTTON,
    /* fb.style.set(style, property, value) */
    FB_METHOD_STYLE_SET,
    FB_METHOD_COUNT
};

#endif /* FB_METHOD_IDS_H */
