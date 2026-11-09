/*
 * DisplayItem Binding - Header
 * C-compatible interface to C++ DisplayItem components
 */

#ifndef DISPLAYITEM_BINDING_H
#define DISPLAYITEM_BINDING_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DisplayItem information structure */
typedef struct {
    unsigned long id;
    int x;
    int y;
    int width;
    int boxType;
    int fontSize;
    int alignment;
    const char* label;
    const char* unit;
    int round;
    int page;
} DisplayItemInfo;

/*
 * Create a new display item with the specified properties
 * Returns the ID of the created item, or 0 on failure
 */
unsigned long displayitem_create(
    int x, int y, int width, int boxType,
    int fontSize, int alignment,
    const char* label, const char* unit,
    int round, int page
);

/*
 * Remove a display item by ID
 * Returns true if successful, false if item not found
 */
bool displayitem_remove(unsigned long id);

/*
 * Display item id for a config.json "name" (len bytes, no NUL needed).
 * Returns false when no display item has that name.
 */
bool displayitem_find_by_name(const char* name, size_t len, unsigned long* id);

/* Get the total number of display items */
int displayitem_count(void);

/*
 * Get display item information by ID
 * Returns pointer to static DisplayItemInfo struct, or NULL if not found
 * WARNING: Do not free() the returned pointer or its string fields
 */
const DisplayItemInfo* displayitem_get_info(unsigned long id);

/*
 * Update display item label (caption)
 * Returns true if successful, false if item not found
 */
bool displayitem_set_label(unsigned long id, const char* label);

/*
 * Update display item value (main text content)
 * Returns true if successful, false if item not found
 */
bool displayitem_set_value(unsigned long id, const char* value);

/*
 * Update display item unit (hint text)
 * Returns true if successful, false if item not found
 */
bool displayitem_set_unit(unsigned long id, const char* unit);

/*
 * Update display item position
 * Returns true if successful, false if item not found
 */
bool displayitem_set_position(unsigned long id, int x, int y);

/*
 * Update display item page
 * Returns true if successful, false if item not found
 */
bool displayitem_set_page(unsigned long id, int page);

/*
 * Show a page of the main display: items (and the clock) whose page is this
 * page or 0 ("every page") are shown, the others hidden. Any task; the LVGL
 * task applies it on its next tick. Returns false for page < 1.
 */
bool displayitem_show_page(int page);

/*
 * Page of the main display currently shown (1 at boot).
 */
int displayitem_get_shown_page(void);

/*
 * Get all display item IDs
 * Fills the provided array with item IDs, up to maxCount
 * Returns the actual number of items copied
 */
int displayitem_get_all_ids(unsigned long* ids, int maxCount);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAYITEM_BINDING_H */
