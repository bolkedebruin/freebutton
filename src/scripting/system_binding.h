#ifndef SYSTEM_BINDING_H
#define SYSTEM_BINDING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint32_t system_hw_get_uptime(void);
uint32_t system_hw_get_free_heap(void);
const char* system_hw_get_version(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_BINDING_H
