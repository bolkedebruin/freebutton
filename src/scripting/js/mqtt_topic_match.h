/*
 * Topic filter match for the JS MQTT layer. Pure C, header-only, so the
 * native tests can include it.
 *
 * filter: NUL-terminated MQTT subscription filter, matched level by level:
 *         '+' matches exactly one level ("a/+/c" matches "a/x/c" and "a//c",
 *         not "a/c" or "a/x/y/c"); a trailing '#' matches the rest, including
 *         the parent level ("a/b/#" matches "a/b" and "a/b/...", not "a/bc";
 *         "#" matches all). A '+' or '#' that is not a whole level is literal.
 *         Topics starting with '$' do not match a leading wildcard (MQTT 4.7.2).
 * topic:  pointer + length, not NUL-terminated (MQTT receive buffer).
 */
#ifndef FREEBUTTON_MQTT_TOPIC_MATCH_H
#define FREEBUTTON_MQTT_TOPIC_MATCH_H

#include <stddef.h>
#include <string.h>

static inline int mqtt_topic_matches(const char *filter, const char *topic, size_t topic_len) {
    const char *f = filter;
    const char *t = topic;
    const char *const end = topic + topic_len;

    if (topic_len > 0 && topic[0] == '$' && (f[0] == '+' || f[0] == '#'))
        return 0;

    for (;;) {
        /* One filter level against one topic level. */
        if (f[0] == '#' && f[1] == '\0')
            return 1; /* the rest of the topic, any number of levels */
        if (f[0] == '+' && (f[1] == '/' || f[1] == '\0')) {
            while (t < end && *t != '/')
                t++;
            f++;
        } else {
            while (*f != '\0' && *f != '/') {
                if (t == end || *t != *f)
                    return 0;
                f++;
                t++;
            }
            if (t < end && *t != '/')
                return 0; /* topic level is longer */
        }

        /* Both are at the end of a level. */
        if (*f == '\0')
            return t == end;
        if (t == end)
            return f[1] == '#' && f[2] == '\0'; /* "a/#" also matches "a" */
        f++; /* '/' */
        t++; /* '/' */
    }
}

#endif /* FREEBUTTON_MQTT_TOPIC_MATCH_H */
