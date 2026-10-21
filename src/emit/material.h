#pragma once

#include <stddef.h>

// MaterialRole fills one Material 3 color role from a shadcn token. The two
// toolkits name a few roles differently, and Flutter has deprecated some
// that Compose keeps; such a role's Flutter name is nullptr.
typedef struct MaterialRole {
    const char *compose;
    const char *flutter;
    const char *token;
} MaterialRole;

extern const MaterialRole MATERIAL_ROLES[];
extern const size_t MATERIAL_ROLE_COUNT;
