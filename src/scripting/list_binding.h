//
// List binding - C interface to the main-display lists (components/List.h)
// for the JS API fb.main.list (freebutton_device.c).
//
// A list is addressed by its index in config.json "lists"; list_hw_find()
// resolves a config name. Any task (scripts, JS engine lock held); nothing
// here takes the LVGL lock.
//

#ifndef LIST_BINDING_H
#define LIST_BINDING_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Index of the list with this config name (len bytes), -1 when none.
int list_hw_find(const char* name, size_t len);

// Number of items, -1 when there is no list at index.
int list_hw_count(int index);

// Selected item (-1 when the list is empty or does not exist).
int list_hw_get_selected(int index);

// Moves the selection by step (clamped); returns the new selection, -1 when
// the list is empty or does not exist.
int list_hw_move(int index, int step);

// false when there is no list at index or item is out of range.
bool list_hw_select(int index, int item);

// Items for list_hw_items_commit(): a builder in the general heap.
void* list_hw_items_new(void);
// false when the builder is full (list::kMaxItems); the item is not added.
bool list_hw_items_add(void* builder, const char* text, size_t len);
// Replaces the items of the list at index and frees the builder (also when
// it returns false: no list at index).
bool list_hw_items_commit(int index, void* builder);
// Frees a builder without using it (an error while reading the items).
void list_hw_items_free(void* builder);

#ifdef __cplusplus
}
#endif

#endif // LIST_BINDING_H
