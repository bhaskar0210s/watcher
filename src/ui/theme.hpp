#pragma once

#include "../core/cf.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include <array>

namespace watcher {

// Colors, fonts and icons resolved for one appearance (light or dark). The
// palette is deliberately quiet: neutral greys, the system accent colour for
// history charts only, and red only for values that need attention.
struct Theme {
    bool dark = true;

    // Surfaces. `background` is only painted where there is no system material
    // (snapshots); in the app the popup sits on the native popover material.
    CFRef<CGColorRef> background, platter, segmentTrack, segmentSelected, separator, track;
    // Text and data.
    CFRef<CGColorRef> text, secondary, tertiary, critical, accent, fill, fillSoft;
    // Memory breakdown, as three shades of one neutral.
    std::array<CFRef<CGColorRef>, 3> memory;
    // Menu bar foreground (white on a dark bar, black on a light one).
    CFRef<CGColorRef> menuText;

    CFRef<CTFontRef> heroFont, heroUnitFont, heroTitleFont, captionFont, cardTitleFont, rowFont, rowValueFont,
        smallFont, smallValueFont, tabNameFont, tabValueFont, menuValueFont;
    // Width of the widest menu bar number ("100%"), so the item never changes width.
    CGFloat menuValueWidth = 0;

    // SF Symbols for the CPU / RAM / GPU / Sensors tabs (plain and selected), the
    // menu bar, and the "more" button.
    std::array<CFRef<CGImageRef>, 4> tabIcons, selectedTabIcons, menuIcons;
    CFRef<CGImageRef> moreIcon;
};

}  // namespace watcher
