#pragma once

#include "../core/cf.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include <string>

namespace watcher {

enum class Align { Left, Center, Right };

struct TextStyle {
    CTFontRef font;
    CGColorRef color;
    CGFloat kern = 0;
};

CFRef<CTLineRef> makeLine(const std::string& text, const TextStyle& style);
CGFloat lineWidth(CTLineRef line);
// Draws `line` with its baseline at y. `flipped` must match the context's y direction.
void drawLine(CGContextRef g, CTLineRef line, CGFloat x, CGFloat baseline, bool flipped);
// Draws text centred vertically in `box` of a flipped (y-down) context.
// Returns the drawn width; with g == nullptr it only measures.
CGFloat drawText(CGContextRef g, const std::string& text, const TextStyle& style, CGRect box, Align align,
                 bool truncate = false);
// Same for a context with AppKit's default y-up orientation.
CGFloat drawTextUp(CGContextRef g, const std::string& text, const TextStyle& style, CGRect box, Align align);

CFRef<CGPathRef> roundedPath(CGRect rect, CGFloat radius);
CFRef<CGColorRef> withAlpha(CGColorRef color, CGFloat alpha);
void fillRect(CGContextRef g, CGRect rect, CGColorRef color);
void fillRounded(CGContextRef g, CGRect rect, CGFloat radius, CGColorRef color);
// Fills `rect` (within the current clip) with a vertical top-to-bottom gradient.
void fillVerticalGradient(CGContextRef g, CGRect rect, CGColorRef top, CGColorRef bottom, bool flipped);
// Draws an image upright into `rect` of a flipped context.
void drawImageFlipped(CGContextRef g, CGImageRef image, CGRect rect);

}  // namespace watcher
