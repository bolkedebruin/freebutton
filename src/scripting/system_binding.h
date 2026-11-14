#ifndef SYSTEM_BINDING_H
#define SYSTEM_BINDING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

uint32_t system_hw_get_uptime(void);
uint32_t system_hw_get_free_heap(void);
const char* system_hw_get_version(void);
// Static storage, valid for the device's lifetime.
const char* system_hw_get_id(void);
const char* system_hw_get_model(void);
// The name from config.json "core.name" (the id without one): copied into
// buf, at most cap bytes, no terminator. Returns the number of bytes.
size_t system_hw_get_name(char* buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_BINDING_H
