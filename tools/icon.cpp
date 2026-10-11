// Renders Watcher's app icon into an .iconset directory (see `make icon`):
// an eye whose iris is a gauge, on a graphite tile. Flat colours keep it tiny.

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace {

CGColorRef color(int rgb, double alpha = 1) {
    return CGColorCreateSRGB(((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0, alpha);
}

void fill(CGContextRef g, CGPathRef path, int rgb, double alpha = 1) {
    CGColorRef c = color(rgb, alpha);
    CGContextAddPath(g, path);
    CGContextSetFillColorWithColor(g, c);
    CGContextFillPath(g);
    CGColorRelease(c);
}

void circle(CGContextRef g, double cx, double cy, double r, int rgb, double alpha = 1) {
    CGPathRef path = CGPathCreateWithEllipseInRect(CGRectMake(cx - r, cy - r, 2 * r, 2 * r), nullptr);
    fill(g, path, rgb, alpha);
    CGPathRelease(path);
}

void arc(CGContextRef g, double cx, double cy, double r, double from, double to, double width, int rgb) {
    CGMutablePathRef path = CGPathCreateMutable();
    CGPathAddArc(path, nullptr, cx, cy, r, from * M_PI / 180, to * M_PI / 180, true);
    CGPathRef stroked = CGPathCreateCopyByStrokingPath(path, nullptr, width, kCGLineCapRound, kCGLineJoinRound, 0);
    fill(g, stroked, rgb);
    CGPathRelease(stroked);
    CGPathRelease(path);
}

void render(int pixels, const std::string& file) {
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef g = CGBitmapContextCreate(nullptr, pixels, pixels, 8, 0, space, kCGImageAlphaPremultipliedLast);
    CGContextScaleCTM(g, pixels / 1024.0, pixels / 1024.0);

    // macOS icon grid: an 824pt rounded square centred in a 1024pt canvas.
    CGPathRef tile = CGPathCreateWithRoundedRect(CGRectMake(100, 100, 824, 824), 185, 185, nullptr);
    fill(g, tile, 0x1C1C1E);

    // The eye: an almond of two curves meeting at the corners.
    CGMutablePathRef eye = CGPathCreateMutable();
    CGPathMoveToPoint(eye, nullptr, 214, 512);
    CGPathAddQuadCurveToPoint(eye, nullptr, 512, 812, 810, 512);
    CGPathAddQuadCurveToPoint(eye, nullptr, 512, 212, 214, 512);
    CGPathCloseSubpath(eye);
    fill(g, eye, 0xF2F2F7);
    CGPathRelease(eye);

    // The iris is a gauge: a dark disc with a 270° scale and a blue reading.
    circle(g, 512, 512, 156, 0x1C1C1E);
    arc(g, 512, 512, 112, 225, -45, 34, 0x3A3A3C);
    arc(g, 512, 512, 112, 225, 48, 34, 0x0A84FF);
    circle(g, 512, 512, 34, 0xF2F2F7);
    circle(g, 566, 568, 16, 0xFFFFFF, 0.85);

    CGImageRef image = CGBitmapContextCreateImage(g);
    CFStringRef path = CFStringCreateWithCString(nullptr, file.c_str(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, path, kCFURLPOSIXPathStyle, false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
    CGImageDestinationAddImage(dest, image, nullptr);
    CGImageDestinationFinalize(dest);

    CFRelease(dest);
    CFRelease(url);
    CFRelease(path);
    CGImageRelease(image);
    CGPathRelease(tile);
    CGContextRelease(g);
    CGColorSpaceRelease(space);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: icon <dir.iconset>\n");
        return 1;
    }
    const std::string dir = argv[1];
    // Sizes above 256pt@2x are left out on purpose: Finder scales up, and the app stays small.
    const struct { int points, scale; } sizes[] = {{16, 1}, {16, 2}, {32, 1}, {32, 2}, {128, 1}, {128, 2}, {256, 1}, {256, 2}};
    for (const auto& size : sizes) {
        char name[64];
        snprintf(name, sizeof(name), "/icon_%dx%d%s.png", size.points, size.points, size.scale == 2 ? "@2x" : "");
        render(size.points * size.scale, dir + name);
    }
    return 0;
}
