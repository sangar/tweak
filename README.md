# tweak

Turns a [tweakcn](https://tweakcn.com) theme into theme code for native apps:
SwiftUI and asset catalogs for iOS and macOS, Jetpack Compose and resource
files for Android, Flutter, and plain JSON for anything else.

```sh
tweak -f swift modern-minimal > ModernMinimalTheme.swift
tweak -f compose --package com.example.ui.theme https://tweakcn.com/themes/<id> > Theme.kt
tweak -f all --p3 -o generated/ theme.css
```

## Input

The theme argument is any of:

| Input | Example |
|---|---|
| A tweakcn theme name or id | `catppuccin`, `cmlhfpjhw000004l4f8ax3m7k` |
| A tweakcn URL | `https://tweakcn.com/themes/<id>`, `https://tweakcn.com/editor/theme?theme=claude`, `https://tweakcn.com/r/themes/claude.json` |
| A file | the registry JSON, or the CSS from tweakcn's code panel |
| `-` | either of those on standard input |

Names, ids and theme page URLs are fetched from tweakcn's registry endpoint,
`https://tweakcn.com/r/themes/<id>`, with `curl`.

CSS can be the Tailwind 4 export (`oklch()` values) or a Tailwind 3 one
(bare `H S% L%` channels, optionally inside `@layer base`). Colors may be
`oklch()`, `oklab()`, `hsl()`, `rgb()` or hex; `var()` references are
resolved per appearance, so `--ring: var(--primary)` follows the dark
primary in dark mode.

## Formats

| Format | Writes | Use it with |
|---|---|---|
| `swift` | `<Name>Theme.swift` | `ModernMinimalTheme.Colors.primary`, `.Radius.lg`, `.Fonts.sans`; colors switch with the system appearance on iOS, iPadOS, tvOS, visionOS and macOS (iOS 15, macOS 12) |
| `xcassets` | `<Name>Theme.xcassets/` | one color set per token with a dark appearance: `Color("primary")`, or Xcode's generated asset symbols |
| `compose` | `<Name>Theme.kt` | wrap content in `ModernMinimalTheme { }`; tokens via `ModernMinimalTheme.colors.sidebarAccent`, Material components get a filled `ColorScheme` and `Shapes` |
| `android` | `values/<name>_theme.xml`, `values-night/<name>_theme.xml` | `@color/primary`, `@dimen/radius_lg` in views and XML themes |
| `flutter` | `<name>_theme.dart` | `ThemeData` from `ModernMinimalTheme.light()` / `.dark()`; tokens via `Theme.of(context).extension<ModernMinimalColors>()!` |
| `json` | `<name>.json` | sRGB hex colors per appearance, the radius scale and font names |

A single-file format prints to standard output unless `-o DIR` is given;
`xcassets`, `android` and several formats at once need `-o`.

Every token in the theme is carried over, including `sidebar-*`, `chart-*`
and any custom variable whose value is a color. Identifiers are the token
names in the target's style: `card-foreground` becomes `cardForeground`, or
`card_foreground` in Android resources. JSON keeps tweakcn's names.

### Color

tweakcn themes are written in OKLCH, which can describe colors outside sRGB.
By default every color is converted to sRGB using the CSS Color 4 gamut
mapping: chroma is reduced at constant lightness and hue, so a saturated
color stays the same hue instead of shifting the way per-channel clipping
does. With `--p3`, colors are mapped into Display P3 instead and written in
that space for `swift`, `xcassets`, `compose` and `flutter`, which keeps more
of the saturated accents on wide-gamut screens. Android resources and JSON
are always sRGB.

### Material 3

`compose` and `flutter` fill the Material color roles from shadcn's tokens,
only where the theme has the token:

| Material | shadcn |
|---|---|
| primary, primaryContainer / on… | primary / primary-foreground |
| secondary, secondaryContainer / on… | secondary / secondary-foreground |
| tertiary, tertiaryContainer / on… | accent / accent-foreground |
| error, errorContainer / on… | destructive / destructive-foreground |
| background, surface, surfaceTint / onSurface | background / foreground |
| surfaceVariant / onSurfaceVariant | muted / muted-foreground |
| surfaceContainerLowest, Low, Highest | card |
| surfaceContainer, High | popover |
| inverseSurface / inverseOnSurface | foreground / background |
| outline | input |
| outlineVariant | border |

Surface tint is the background so elevation does not tint surfaces with the
primary color. Flutter skips the roles it has deprecated (background,
onBackground, surfaceVariant) and sets `scaffoldBackgroundColor` instead.

### Radius and fonts

`--radius` becomes shadcn's scale, as tweakcn's `@theme` block derives it:
sm = radius − 4, md = radius − 2, lg = radius, xl = radius + 4, in points or
dp, never below 0. Compose maps them to Material `Shapes` from extraSmall to
extraLarge.

Fonts are the first family of `--font-sans`, `--font-serif` and
`--font-mono`; a stack that starts with a generic family such as
`ui-sans-serif` means the system font and is written as `nil` or `null`. The
font files themselves are not part of a theme: bundle them, or load them
with downloadable fonts or the `google_fonts` package.

Not translated: shadows, letter spacing and spacing, which have no common
equivalent across the targets.

## Profile

| | |
|---|---|
| Profile | Modern C Level 2 |
| Language | C23 |
| Compilers | clang 18 or newer (primary), gcc 14 or newer |
| Targets | Linux x86_64 and aarch64 (static, musl), macOS arm64 and x86_64 |
| Build | `tools/build.c`, a build program in C |
| Dependencies | [libmc](https://github.com/sangar/libmc), vendored in `deps/libmc` and pinned in `deps.lock` |

Memory lives in one arena, fallible functions return libmc's `Error` with
an `Err` message, and there is no global mutable state. The build uses
`-Werror -Wall -Wextra -Wshadow -Wconversion -Wvla -Wstrict-prototypes
-Wimplicit-fallthrough`, and the tests run under AddressSanitizer and
UndefinedBehaviorSanitizer.

Windows is not a target: libmc has no Windows platform layer.

Deliberate departures from the profile: downloads run the system `curl`
through libmc's `process_run`, since libmc has no HTTP client yet; and the
tests' `CHECK` macros are function-like so a failure can name its line.

## Build

The build program is C and rebuilds itself when it changes. Bootstrap it
once:

```sh
cc tools/build.c -o nob
```

```sh
./nob             # ./tweak, -O2 -g
./nob test        # the tests under AddressSanitizer and UndefinedBehaviorSanitizer
./nob check       # the modern-c contract check, then the tests
./nob cross       # out/<target>/tweak for every target, with the zig in mise.toml
./nob clean
VERSION=1.0.0 ./nob   # what `tweak --version` prints (default: git describe or "dev")
```

libmc is linked as a static archive, so only the modules tweak calls end up
in the binary; it needs no macOS framework, which lets zig link the macOS
binaries without the SDK.

## Install

```sh
./nob install      # ~/.local/bin/tweak
./nob uninstall
```

Install elsewhere with `PREFIX=/usr/local ./nob install`, and uninstall with
the same `PREFIX`. `DESTDIR` is honoured for packaging.
