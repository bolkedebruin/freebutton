/*
 * Topic filter match for the JS MQTT layer. Pure C, header-only, so the
 * native tests can include it.
 *
 * filter: NUL-terminated subscription filter. Exact match, or a trailing '#'
 *         ("a/b/#" matches "a/b" and "a/b/...", not "a/bc"; "#" matches all).
 *         '+' is not supported.
 * topic:  pointer + length, not NUL-terminated (MQTT receive buffer).
 */
#ifndef FREEBUTTON_MQTT_TOPIC_MATCH_H
#define FREEBUTTON_MQTT_TOPIC_MATCH_H

#include <stddef.h>
#include <string.h>

static inline int mqtt_topic_matches(const char *filter, const char *topic, size_t topic_len) {
    const size_t filter_len = strlen(filter);
    if (filter_len == topic_len && memcmp(filter, topic, topic_len) == 0)
        return 1;

    if (filter_len == 0 || filter[filter_len - 1] != '#')
        return 0;

    if (filter_len == 1)
        return 1; /* "#" */

    size_t prefix_len = filter_len - 1;
    if (filter[prefix_len - 1] == '/')
        prefix_len--;
    return prefix_len <= topic_len && memcmp(filter, topic, prefix_len) == 0 &&
           (topic_len == prefix_len || topic[prefix_len] == '/');
}

#endif /* FREEBUTTON_MQTT_TOPIC_MATCH_H */
