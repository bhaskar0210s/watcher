#include "platform.hpp"

#include "../ui/format.hpp"

#include <cmath>

namespace watcher {

Themes gThemes;

namespace {

bool isDark(NSAppearance* appearance) {
    return [[appearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]]
        isEqualToString:NSAppearanceNameDarkAqua];
}

CFRef<CGColorRef> cg(NSColor* color) {
    return CFRef<CGColorRef>::retain(color.CGColor);
}

// A system font, optionally SF Pro Rounded and with fixed-width digits so
// numbers don't jiggle as they change.
CFRef<CTFontRef> makeFont(CGFloat size, NSFontWeight weight, bool rounded = false, bool tabularDigits = false) {
    NSFont* font = [NSFont systemFontOfSize:size weight:weight];
    NSFontDescriptor* descriptor = font.fontDescriptor;
    if (rounded) descriptor = [descriptor fontDescriptorWithDesign:NSFontDescriptorSystemDesignRounded] ?: descriptor;
    if (tabularDigits) {
        descriptor = [descriptor fontDescriptorByAddingAttributes:@{
            NSFontFeatureSettingsAttribute : @[ @{
                NSFontFeatureTypeIdentifierKey : @(kNumberSpacingType),
                NSFontFeatureSelectorIdentifierKey : @(kMonospacedNumbersSelector)
            } ]
        }];
    }
    NSFont* result = [NSFont fontWithDescriptor:descriptor size:size] ?: font;
    return CFRef<CTFontRef>((CTFontRef)CFBridgingRetain(result));
}

// An SF Symbol drawn into a square @2x image of `side` points, tinted with `color`.
CFRef<CGImageRef> renderSymbol(NSString* name, CGColorRef color, CGFloat side, CGFloat pointSize) {
    constexpr CGFloat kScale = 2;
    NSImage* symbol = [[NSImage imageWithSystemSymbolName:name accessibilityDescription:nil]
        imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:pointSize
                                                                                     weight:NSFontWeightMedium]];
    CFRef<CGColorSpaceRef> space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    CFRef<CGContextRef> context(CGBitmapContextCreate(
        nullptr, size_t(side * kScale), size_t(side * kScale), 8, 0, space.get(),
        uint32_t(kCGImageAlphaPremultipliedFirst) | uint32_t(kCGBitmapByteOrder32Host)));
    if (!symbol || !context) return {};
    CGContextScaleCTM(context.get(), kScale, kScale);
    const NSSize size = symbol.size;
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:context.get() flipped:NO];
    [symbol drawInRect:NSMakeRect((side - size.width) / 2, (side - size.height) / 2, size.width, size.height)];
    [NSGraphicsContext restoreGraphicsState];
    CGContextSetBlendMode(context.get(), kCGBlendModeSourceIn);  // keep the glyph's alpha, swap its colour
    CGContextSetFillColorWithColor(context.get(), color);
    CGContextFillRect(context.get(), CGRectMake(0, 0, side, side));
    return CFRef<CGImageRef>(CGBitmapContextCreateImage(context.get()));
}

Theme makeTheme(bool dark) {
    Theme t;
    t.dark = dark;
    Theme* out = &t;
    NSAppearance* appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    // Dynamic system colors resolve against the current drawing appearance.
    [appearance performAsCurrentDrawingAppearance:^{
        Theme& th = *out;
        // Neutral ink: white on dark, black on light, at a few strengths.
        auto ink = [dark](CGFloat alpha) { return cg([NSColor colorWithWhite:dark ? 1 : 0 alpha:alpha]); };
        th.background = cg(dark ? [NSColor colorWithSRGBRed:0.15 green:0.15 blue:0.16 alpha:1]
                                : [NSColor colorWithSRGBRed:0.95 green:0.95 blue:0.96 alpha:1]);
        th.platter = dark ? ink(0.06) : cg([NSColor colorWithWhite:1 alpha:0.7]);
        th.segmentTrack = ink(dark ? 0.07 : 0.05);
        th.segmentSelected = dark ? ink(0.17) : cg(NSColor.whiteColor);
        th.separator = cg(NSColor.separatorColor);
        th.track = ink(dark ? 0.12 : 0.08);
        th.text = cg(NSColor.labelColor);
        th.secondary = cg(NSColor.secondaryLabelColor);
        th.tertiary = cg(NSColor.tertiaryLabelColor);
        th.critical = cg(NSColor.systemRedColor);
        th.accent = cg(NSColor.controlAccentColor);
        th.fill = ink(dark ? 0.88 : 0.62);
        th.fillSoft = ink(dark ? 0.45 : 0.30);
        th.memory = {ink(dark ? 0.88 : 0.62), ink(dark ? 0.50 : 0.38), ink(dark ? 0.26 : 0.20)};
        th.menuText = cg(dark ? NSColor.whiteColor : NSColor.blackColor);
    }];
    t.heroFont = makeFont(30, NSFontWeightSemibold, false, true);
    t.heroUnitFont = makeFont(17, NSFontWeightSemibold);
    t.heroTitleFont = makeFont(13, NSFontWeightSemibold);
    t.captionFont = makeFont(12, NSFontWeightRegular);
    t.cardTitleFont = makeFont(12, NSFontWeightSemibold);
    t.rowFont = makeFont(13, NSFontWeightRegular);
    t.rowValueFont = makeFont(13, NSFontWeightMedium, false, true);
    t.smallFont = makeFont(11, NSFontWeightRegular);
    t.smallValueFont = makeFont(11, NSFontWeightMedium, false, true);
    t.tabNameFont = makeFont(11, NSFontWeightMedium);
    t.tabValueFont = makeFont(14, NSFontWeightSemibold, false, true);
    t.menuValueFont = makeFont(13, NSFontWeightMedium, false, true);

    static NSString* const symbols[] = {@"cpu", @"memorychip", @"triangle", @"thermometer.medium"};
    for (size_t i = 0; i < 4; ++i) {
        t.tabIcons[i] = renderSymbol(symbols[i], t.secondary.get(), 13, 10);
        t.selectedTabIcons[i] = renderSymbol(symbols[i], t.text.get(), 13, 10);
        t.menuIcons[i] = renderSymbol(symbols[i], t.menuText.get(), 15, 12);
    }
    t.moreIcon = renderSymbol(@"ellipsis.circle", t.secondary.get(), 18, 15);
    return t;
}

CFRef<CGImageRef> renderIcon(NSImage* image, CGFloat points, CGFloat scale) {
    const size_t pixels = size_t(std::lround(points * scale));
    CFRef<CGColorSpaceRef> space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    CFRef<CGContextRef> context(CGBitmapContextCreate(
        nullptr, pixels, pixels, 8, 0, space.get(),
        uint32_t(kCGImageAlphaPremultipliedFirst) | uint32_t(kCGBitmapByteOrder32Host)));
    if (!context || !image) return {};
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:context.get() flipped:NO];
    [image drawInRect:NSMakeRect(0, 0, pixels, pixels)
             fromRect:NSZeroRect
            operation:NSCompositingOperationSourceOver
             fraction:1];
    [NSGraphicsContext restoreGraphicsState];
    return CFRef<CGImageRef>(CGBitmapContextCreateImage(context.get()));
}

}  // namespace

const Theme& Themes::get(bool dark) {
    std::unique_ptr<Theme>& slot = dark ? dark_ : light_;
    if (!slot) slot = std::make_unique<Theme>(makeTheme(dark));
    return *slot;
}

const Theme& Themes::forAppearance(NSAppearance* appearance) {
    return get(isDark(appearance));
}

void Themes::reset() {
    dark_.reset();
    light_.reset();
}

ProcessRow ProcessDirectory::row(const ProcessUsage& usage, Tab tab, CGFloat scale) {
    auto it = cache_.find(usage.pid);
    if (it == cache_.end()) it = cache_.emplace(usage.pid, lookup(usage.pid, scale)).first;
    ProcessRow row;
    row.name = it->second.name.empty() ? usage.name : it->second.name;
    row.value = tab == Tab::Cpu ? formatProcessCpu(usage.value) : formatBytes(usage.value);
    row.icon = it->second.icon;
    return row;
}

void ProcessDirectory::clear() {
    cache_.clear();
    genericIcon_ = {};
}

ProcessDirectory::Identity ProcessDirectory::lookup(pid_t pid, CGFloat scale) {
    Identity identity;
    @autoreleasepool {
        // Apps get their real name and icon; everything else the Terminal "exec" icon.
        NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
        NSString* name = app.localizedName;
        // Virtualization.framework hosts show up as "Virtual Machine Service for Docker".
        static NSString* const vmPrefix = @"Virtual Machine Service for ";
        if ([name hasPrefix:vmPrefix]) name = [[name substringFromIndex:vmPrefix.length] stringByAppendingString:@" (VM)"];
        if (name.length) identity.name = name.UTF8String;
        if (app.icon) {
            identity.icon = renderIcon(app.icon, 16, scale);
        } else {
            if (!genericIcon_) genericIcon_ = renderIcon([NSWorkspace.sharedWorkspace iconForFile:@"/bin/bash"], 16, scale);
            identity.icon = genericIcon_;
        }
    }
    return identity;
}

}  // namespace watcher
