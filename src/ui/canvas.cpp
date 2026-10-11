#include "canvas.hpp"

#include <algorithm>
#include <cmath>

namespace watcher {

CFRef<CTLineRef> makeLine(const std::string& text, const TextStyle& style) {
    CFRef<CFStringRef> string(CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(text.data()),
                                                      CFIndex(text.size()), kCFStringEncodingUTF8, false));
    if (!string) return {};
    CFRef<CFNumberRef> kern(CFNumberCreate(nullptr, kCFNumberCGFloatType, &style.kern));
    const void* keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName, kCTKernAttributeName};
    const void* values[] = {style.font, style.color, kern.get()};
    CFRef<CFDictionaryRef> attributes(CFDictionaryCreate(nullptr, keys, values, style.kern != 0 ? 3 : 2,
                                                         &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks));
    CFRef<CFAttributedStringRef> attributed(CFAttributedStringCreate(nullptr, string.get(), attributes.get()));
    return CFRef<CTLineRef>(CTLineCreateWithAttributedString(attributed.get()));
}

CGFloat lineWidth(CTLineRef line) {
    return line ? CGFloat(CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr)) : 0;
}

void drawLine(CGContextRef g, CTLineRef line, CGFloat x, CGFloat baseline, bool flipped) {
    if (!g || !line) return;
    // The text matrix is not part of the graphics state, so always set it.
    CGContextSetTextMatrix(g, flipped ? CGAffineTransformMakeScale(1, -1) : CGAffineTransformIdentity);
    CGContextSetTextPosition(g, x, baseline);
    CTLineDraw(line, g);
}

namespace {

CFRef<CTLineRef> truncatedLine(const CFRef<CTLineRef>& line, CGFloat maxWidth, const TextStyle& style) {
    if (lineWidth(line.get()) <= maxWidth) return line;
    CFRef<CTLineRef> ellipsis = makeLine("…", style);
    CFRef<CTLineRef> cut(CTLineCreateTruncatedLine(line.get(), double(maxWidth), kCTLineTruncationEnd, ellipsis.get()));
    return cut ? cut : ellipsis;
}

CGFloat alignedX(CGRect box, CGFloat width, Align align) {
    switch (align) {
        case Align::Left: return box.origin.x;
        case Align::Center: return box.origin.x + std::round((box.size.width - width) / 2);
        case Align::Right: return box.origin.x + box.size.width - width;
    }
    return box.origin.x;
}

}  // namespace

CGFloat drawText(CGContextRef g, const std::string& text, const TextStyle& style, CGRect box, Align align,
                 bool truncate) {
    CFRef<CTLineRef> line = makeLine(text, style);
    if (truncate) line = truncatedLine(line, box.size.width, style);
    const CGFloat width = lineWidth(line.get());
    const CGFloat cap = CTFontGetCapHeight(style.font);
    const CGFloat baseline = std::round((box.origin.y + (box.size.height + cap) / 2) * 2) / 2;
    drawLine(g, line.get(), alignedX(box, width, align), baseline, true);
    return width;
}

CGFloat drawTextUp(CGContextRef g, const std::string& text, const TextStyle& style, CGRect box, Align align) {
    CFRef<CTLineRef> line = makeLine(text, style);
    const CGFloat width = lineWidth(line.get());
    const CGFloat cap = CTFontGetCapHeight(style.font);
    const CGFloat baseline = std::round((box.origin.y + (box.size.height - cap) / 2) * 2) / 2;
    drawLine(g, line.get(), alignedX(box, width, align), baseline, false);
    return width;
}

CFRef<CGPathRef> roundedPath(CGRect rect, CGFloat radius) {
    radius = std::max<CGFloat>(0, std::min({radius, rect.size.width / 2, rect.size.height / 2}));
    if (radius <= 0) return CFRef<CGPathRef>(CGPathCreateWithRect(rect, nullptr));
    return CFRef<CGPathRef>(CGPathCreateWithRoundedRect(rect, radius, radius, nullptr));
}

CFRef<CGColorRef> withAlpha(CGColorRef color, CGFloat alpha) {
    return CFRef<CGColorRef>(CGColorCreateCopyWithAlpha(color, alpha));
}

void fillRect(CGContextRef g, CGRect rect, CGColorRef color) {
    if (!g || rect.size.width <= 0 || rect.size.height <= 0) return;
    CGContextSetFillColorWithColor(g, color);
    CGContextFillRect(g, rect);
}

void fillRounded(CGContextRef g, CGRect rect, CGFloat radius, CGColorRef color) {
    if (!g || rect.size.width <= 0 || rect.size.height <= 0) return;
    CFRef<CGPathRef> path = roundedPath(rect, radius);
    CGContextAddPath(g, path.get());
    CGContextSetFillColorWithColor(g, color);
    CGContextFillPath(g);
}

void fillVerticalGradient(CGContextRef g, CGRect rect, CGColorRef top, CGColorRef bottom, bool flipped) {
    if (!g) return;
    const void* colors[] = {top, bottom};
    CFRef<CFArrayRef> array(CFArrayCreate(nullptr, colors, 2, &kCFTypeArrayCallBacks));
    const CGFloat locations[] = {0, 1};
    CFRef<CGGradientRef> gradient(CGGradientCreateWithColors(CGColorGetColorSpace(top), array.get(), locations));
    if (!gradient) return;
    const CGFloat topY = flipped ? CGRectGetMinY(rect) : CGRectGetMaxY(rect);
    const CGFloat bottomY = flipped ? CGRectGetMaxY(rect) : CGRectGetMinY(rect);
    CGContextDrawLinearGradient(g, gradient.get(), CGPointMake(0, topY), CGPointMake(0, bottomY),
                                kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
}

void drawImageFlipped(CGContextRef g, CGImageRef image, CGRect rect) {
    if (!g || !image) return;
    CGContextSaveGState(g);
    CGContextTranslateCTM(g, rect.origin.x, rect.origin.y + rect.size.height);
    CGContextScaleCTM(g, 1, -1);
    CGContextSetInterpolationQuality(g, kCGInterpolationHigh);
    CGContextDrawImage(g, CGRectMake(0, 0, rect.size.width, rect.size.height), image);
    CGContextRestoreGState(g);
}

}  // namespace watcher
