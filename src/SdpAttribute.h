// SDP attribute lookup shared by the private protocol extensions.
#pragma once

#include <stddef.h>
#include <string.h>

// Copy the value of `a=<key>:<value>` into `out`, trimmed of spaces and tabs.
// Returns 1 when found once, 0 when absent and -1 when empty, duplicated or
// longer than `capacity - 1`. Only complete SDP lines match.
static inline int LiSdpAttributeValue(const char* sdp, const char* key, char* out, size_t capacity) {
    int found = 0;
    size_t klen = strlen(key);
    for (const char* line = sdp; line && *line;) {
        const char* end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (length && line[length - 1] == '\r') --length;
        if (length >= klen + 3 && !memcmp(line, "a=", 2) &&
            !memcmp(line + 2, key, klen) && line[2 + klen] == ':') {
            const char* value = line + klen + 3;
            size_t valueLength = length - klen - 3;
            while (valueLength && (*value == ' ' || *value == '\t')) { ++value; --valueLength; }
            while (valueLength && (value[valueLength-1] == ' ' || value[valueLength-1] == '\t')) --valueLength;
            if (!valueLength) return -1;
            if (++found > 1 || valueLength >= capacity) return -1;
            memcpy(out, value, valueLength); out[valueLength] = 0;
        }
        line = end ? end + 1 : NULL;
    }
    return found;
}
