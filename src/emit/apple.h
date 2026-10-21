#pragma once

#include "emit/emit.h"

// apple_swift writes <Name>Theme.swift: SwiftUI colors that follow the
// system appearance on iOS, iPadOS, tvOS, visionOS and macOS, with the
// radius scale and font names.
void apple_swift(Arena *arena, const Theme *theme, const EmitOptions *options, Output *output);

// apple_asset_catalog writes <Name>Theme.xcassets with a color set per token,
// each with a dark appearance.
void apple_asset_catalog(Arena *arena, const Theme *theme, const EmitOptions *options, Output *output);
