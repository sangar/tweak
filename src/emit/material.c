#include "emit/material.h"

#include "mc/core/base.h"

// shadcn's tokens map onto Material roles by what draws with them: a
// container is the same fill as its accent, cards and popovers are the
// surface containers, an input's border is Material's outline and a
// divider's its outline variant. Surface tint is the background, so
// elevation does not wash surfaces in the primary color as it would in
// Material's defaults.
const MaterialRole MATERIAL_ROLES[] = {
    { "primary", "primary", "primary" },
    { "onPrimary", "onPrimary", "primary-foreground" },
    { "primaryContainer", "primaryContainer", "primary" },
    { "onPrimaryContainer", "onPrimaryContainer", "primary-foreground" },
    { "secondary", "secondary", "secondary" },
    { "onSecondary", "onSecondary", "secondary-foreground" },
    { "secondaryContainer", "secondaryContainer", "secondary" },
    { "onSecondaryContainer", "onSecondaryContainer", "secondary-foreground" },
    { "tertiary", "tertiary", "accent" },
    { "onTertiary", "onTertiary", "accent-foreground" },
    { "tertiaryContainer", "tertiaryContainer", "accent" },
    { "onTertiaryContainer", "onTertiaryContainer", "accent-foreground" },
    { "background", nullptr, "background" },
    { "onBackground", nullptr, "foreground" },
    { "surface", "surface", "background" },
    { "onSurface", "onSurface", "foreground" },
    { "surfaceVariant", nullptr, "muted" },
    { "onSurfaceVariant", "onSurfaceVariant", "muted-foreground" },
    { "surfaceTint", "surfaceTint", "background" },
    { "surfaceContainerLowest", "surfaceContainerLowest", "card" },
    { "surfaceContainerLow", "surfaceContainerLow", "card" },
    { "surfaceContainer", "surfaceContainer", "popover" },
    { "surfaceContainerHigh", "surfaceContainerHigh", "popover" },
    { "surfaceContainerHighest", "surfaceContainerHighest", "card" },
    { "inverseSurface", "inverseSurface", "foreground" },
    { "inverseOnSurface", "onInverseSurface", "background" },
    { "error", "error", "destructive" },
    { "onError", "onError", "destructive-foreground" },
    { "errorContainer", "errorContainer", "destructive" },
    { "onErrorContainer", "onErrorContainer", "destructive-foreground" },
    { "outline", "outline", "input" },
    { "outlineVariant", "outlineVariant", "border" },
};

const size_t MATERIAL_ROLE_COUNT = countof(MATERIAL_ROLES);
