// The store core, compiled as C for the native tests (the native env builds
// only test/).
#define MEDIA_CORE_STATS
/* The store's heap (free map, index) through the tests' allocator, which
 * counts the bytes and can be made to fail (test_media_store.cpp). */
#include <stddef.h>
void *media_test_alloc(size_t n);
void media_test_free(void *p);
#define MEDIA_ALLOC(n) media_test_alloc(n)
#define MEDIA_FREE(p) media_test_free(p)
#include "../../src/media/media_core.c"
