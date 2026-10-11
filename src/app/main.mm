// Watcher: the AppKit shell. One status item draws the C++-rendered CPU widget;
// clicking it opens a borderless panel with CPU, RAM, GPU and Sensors tabs.

#include "platform.hpp"

#import <QuartzCore/QuartzCore.h>
#import <ServiceManagement/ServiceManagement.h>

#include "../core/engine.hpp"
#include "../core/processes.hpp"
#include "../ui/widgets.hpp"

#include <malloc/malloc.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace watcher;

namespace {

constexpr NSTimeInterval kInterval = 1.0;
constexpr CFTimeInterval kForeignRefresh = 15;  // seconds between /usr/bin/top runs
constexpr CFTimeInterval kWiggleDelay = 2;      // small menu bar changes are coalesced to this
NSString* const kTabKey = @"SelectedTab";
NSString* const kLoginItemKey = @"LoginItemRegistered";

// A stretchable rounded-rect mask for NSVisualEffectView.
NSImage* roundedMask(CGFloat radius) {
    const CGFloat edge = 2 * radius + 1;
    NSImage* mask = [NSImage imageWithSize:NSMakeSize(edge, edge)
                                   flipped:NO
                            drawingHandler:^BOOL(NSRect rect) {
                                [NSColor.blackColor setFill];
                                [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius yRadius:radius] fill];
                                return YES;
                            }];
    mask.capInsets = NSEdgeInsetsMake(radius, radius, radius, radius);
    mask.resizingMode = NSImageResizingModeStretch;
    return mask;
}

CGFloat backingScale(NSWindow* window) {
    const CGFloat scale = window ? window.backingScaleFactor : NSScreen.mainScreen.backingScaleFactor;
    return scale > 0 ? scale : 2;
}

}  // namespace

@interface WTApp : NSObject <NSApplicationDelegate>
- (void)selectTab:(Tab)tab;
- (void)showMenuInView:(NSView*)view at:(NSPoint)point;
@end

#pragma mark - Menu bar widget

@interface WTWidgetView : NSView {
@public
    WidgetData _data;  // what is on screen
}
@end

@implementation WTWidgetView

- (NSView*)hitTest:(NSPoint)point {
    return nil;  // clicks belong to the status bar button underneath
}

- (void)viewDidChangeEffectiveAppearance {
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    drawWidget(NSGraphicsContext.currentContext.CGContext, NSSizeToCGSize(self.bounds.size), _data,
               gThemes.forAppearance(self.effectiveAppearance));
}

@end

#pragma mark - Popup

@interface WTPanel : NSPanel
@end

@implementation WTPanel
- (BOOL)canBecomeKeyWindow {
    return NO;
}
- (BOOL)canBecomeMainWindow {
    return NO;
}
@end

// Lets VoiceOver (and UI scripts) press the custom-drawn tab and menu buttons.
@interface WTAccessibleButton : NSAccessibilityElement
@property(nonatomic, copy) void (^action)(void);
@end

@implementation WTAccessibleButton
- (BOOL)accessibilityPerformPress {
    if (self.action) self.action();
    return self.action != nil;
}
@end

@interface WTPopupView : NSView {
@public
    PopupContent _content;
    PopupHover _hover;
    PopupLayout _layout;
    __weak WTApp* _app;
    NSArray<WTAccessibleButton*>* _buttons;
}
@end

@implementation WTPopupView

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) self.wantsLayer = YES;
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    return YES;
}

- (void)viewDidChangeEffectiveAppearance {
    self.needsDisplay = YES;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    for (NSTrackingArea* area in self.trackingAreas) [self removeTrackingArea:area];
    const NSTrackingAreaOptions options = NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                                          NSTrackingActiveAlways | NSTrackingInVisibleRect;
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect options:options owner:self userInfo:nil]];
}

// The popup is drawn on the CPU into a plain image in the display's colour space,
// which Core Animation shows as is. Drawing through drawRect: instead lets Core
// Animation rasterise on the GPU from inside Watcher, which loads Metal and keeps
// several megabytes of GPU buffers for the rest of the app's life.
- (BOOL)wantsUpdateLayer {
    return YES;
}

- (void)updateLayer {
    const CGFloat scale = backingScale(self.window);
    const CGSize size = NSSizeToCGSize(self.bounds.size);
    CFRef<CGColorSpaceRef> space = CFRef<CGColorSpaceRef>::retain(self.window.colorSpace.CGColorSpace);
    if (!space) space = CFRef<CGColorSpaceRef>(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    CFRef<CGContextRef> g(CGBitmapContextCreate(
        nullptr, size_t(std::ceil(size.width * scale)), size_t(std::ceil(size.height * scale)), 8, 0, space.get(),
        uint32_t(kCGImageAlphaPremultipliedFirst) | uint32_t(kCGBitmapByteOrder32Host)));
    if (!g) return;
    CGContextScaleCTM(g.get(), scale, scale);
    CGContextTranslateCTM(g.get(), 0, size.height);  // y-down, like the view
    CGContextScaleCTM(g.get(), 1, -1);
    CGContextSetShouldSmoothFonts(g.get(), false);  // as AppKit draws text on a transparent layer
    _content.scale = scale;
    _layout = renderPopup(g.get(), _content, _hover, gThemes.forAppearance(self.effectiveAppearance));
    CFRef<CGImageRef> image(CGBitmapContextCreateImage(g.get()));
    self.layer.contentsScale = scale;
    self.layer.contents = (__bridge id)image.get();  // transparent: the popover material shows through
}

- (BOOL)isOverHoverArea:(CGPoint)point {
    for (const CGRect& area : _layout.hoverAreas) {
        if (CGRectContainsPoint(area, point)) return YES;
    }
    return NO;
}

- (int)tabAt:(CGPoint)point {
    for (int i = 0; i < kTabCount; ++i) {
        if (CGRectContainsPoint(_layout.tabs[size_t(i)], point)) return i;
    }
    return -1;
}

- (void)mouseMoved:(NSEvent*)event {
    const CGPoint point = NSPointToCGPoint([self convertPoint:event.locationInWindow fromView:nil]);
    const int tab = [self tabAt:point];
    if (tab >= 0) [_app selectTab:Tab(tab)];  // tabs switch the instant the pointer touches them
    const BOOL wasOver = _hover.inside && [self isOverHoverArea:_hover.point];
    _hover = {true, point};
    if (wasOver || [self isOverHoverArea:point]) self.needsDisplay = YES;
}

- (void)mouseExited:(NSEvent*)event {
    const BOOL wasOver = _hover.inside && [self isOverHoverArea:_hover.point];
    _hover.inside = false;
    if (wasOver) self.needsDisplay = YES;
}

- (void)mouseDown:(NSEvent*)event {
    const CGPoint point = NSPointToCGPoint([self convertPoint:event.locationInWindow fromView:nil]);
    const int tab = [self tabAt:point];
    if (tab >= 0) [_app selectTab:Tab(tab)];
    if (CGRectContainsPoint(_layout.menuButton, point)) [self showMenu];
}

- (void)showMenu {
    [_app showMenuInView:self at:NSMakePoint(CGRectGetMinX(_layout.menuButton), CGRectGetMaxY(_layout.menuButton))];
}

#pragma mark Accessibility

- (BOOL)isAccessibilityElement {
    return YES;
}

- (NSAccessibilityRole)accessibilityRole {
    return NSAccessibilityGroupRole;
}

- (NSString*)accessibilityLabel {
    return @"Watcher";
}

- (NSArray*)accessibilityChildren {
    if (!_buttons) {
        NSMutableArray<WTAccessibleButton*>* buttons = [NSMutableArray array];
        __weak WTPopupView* weakSelf = self;
        for (int i = 0; i < kTabCount; ++i) {
            WTAccessibleButton* tab = [WTAccessibleButton new];
            tab.accessibilityRole = NSAccessibilityRadioButtonRole;
            tab.accessibilityLabel = @(tabName(Tab(i)));
            tab.accessibilityParent = self;
            tab.action = ^{
                WTPopupView* view = weakSelf;
                if (view) [view->_app selectTab:Tab(i)];
            };
            [buttons addObject:tab];
        }
        WTAccessibleButton* menu = [WTAccessibleButton new];
        menu.accessibilityRole = NSAccessibilityButtonRole;
        menu.accessibilityLabel = @"Menu";
        menu.accessibilityParent = self;
        menu.action = ^{
            [weakSelf showMenu];
        };
        [buttons addObject:menu];
        _buttons = buttons;
    }
    if (_layout.height == 0) _layout = renderPopup(nullptr, _content, _hover, gThemes.forAppearance(self.effectiveAppearance));
    for (int i = 0; i < kTabCount; ++i) {
        _buttons[i].accessibilityFrameInParentSpace = NSRectFromCGRect(_layout.tabs[size_t(i)]);
        _buttons[i].accessibilityValue = @(int(_content.tab) == i);
    }
    _buttons[kTabCount].accessibilityFrameInParentSpace = NSRectFromCGRect(_layout.menuButton);
    return _buttons;
}

@end

#pragma mark - App

@implementation WTApp {
    std::unique_ptr<Engine> _engine;
    NSStatusItem* _item;
    WTWidgetView* _widget;
    CFAbsoluteTime _paintedAt;
    NSTimer* _timer;

    WTPanel* _panel;
    WTPopupView* _popup;
    CGFloat _popupX;  // fixed while open, even if the menu bar item changes width
    Tab _tab;
    BOOL _fahrenheit;
    id _clickMonitor;

    dispatch_queue_t _processQueue;
    dispatch_queue_t _topQueue;
    BOOL _processBusy;
    BOOL _topBusy;
    std::vector<ProcessUsage> _foreign;
    CFAbsoluteTime _foreignTime;
    ProcessDirectory _directory;
}

- (void)applicationWillFinishLaunching:(NSNotification*)notification {
    NSString* bundleID = NSBundle.mainBundle.bundleIdentifier;
    if (bundleID && [NSRunningApplication runningApplicationsWithBundleIdentifier:bundleID].count > 1) {
        [NSApp terminate:nil];  // already running
    }
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    _engine = std::make_unique<Engine>();
    _tab = Tab(std::clamp<NSInteger>([NSUserDefaults.standardUserDefaults integerForKey:kTabKey], 0, kTabCount - 1));
    [self readTemperatureUnit];
    dispatch_queue_attr_t utility =
        dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0);
    _processQueue = dispatch_queue_create("watcher.processes", utility);
    _topQueue = dispatch_queue_create("watcher.top", utility);

    _item = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    _item.autosaveName = @"watcher";
    NSStatusBarButton* button = _item.button;
    button.target = self;
    button.action = @selector(itemClicked:);
    [button sendActionOn:NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown];
    button.accessibilityLabel = @"Watcher";
    _widget = [[WTWidgetView alloc] initWithFrame:button.bounds];
    _widget.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [button addSubview:_widget];

    NSNotificationCenter* workspace = NSWorkspace.sharedWorkspace.notificationCenter;
    [workspace addObserver:self selector:@selector(pause:) name:NSWorkspaceWillSleepNotification object:nil];
    [workspace addObserver:self selector:@selector(pause:) name:NSWorkspaceScreensDidSleepNotification object:nil];
    [workspace addObserver:self selector:@selector(resume:) name:NSWorkspaceDidWakeNotification object:nil];
    [workspace addObserver:self selector:@selector(resume:) name:NSWorkspaceScreensDidWakeNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(systemColorsChanged:)
                                               name:NSSystemColorsDidChangeNotification
                                             object:nil];
    [self registerLoginItemOnce];
    [self startTimer];
}

- (void)readTemperatureUnit {
    _fahrenheit =
        [[NSUserDefaults.standardUserDefaults stringForKey:@"AppleTemperatureUnit"] isEqualToString:@"Fahrenheit"];
}

// Start at login by default; "Launch at Login" in the menu turns it off.
// Only from /Applications, so a development build never registers itself.
- (void)registerLoginItemOnce {
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    if ([defaults boolForKey:kLoginItemKey] || ![NSBundle.mainBundle.bundlePath hasPrefix:@"/Applications/"]) return;
    [defaults setBool:YES forKey:kLoginItemKey];
    NSError* error = nil;
    if (![SMAppService.mainAppService registerAndReturnError:&error]) {
        NSLog(@"Watcher: could not enable Launch at Login: %@", error);
    }
}

#pragma mark Sampling

- (void)startTimer {
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:kInterval target:self selector:@selector(tick) userInfo:nil repeats:YES];
    _timer.tolerance = 0.2;  // let macOS coalesce our wakeups with others
    [NSRunLoop.mainRunLoop addTimer:_timer forMode:NSRunLoopCommonModes];
    [self tick];
}

- (void)stopTimer {
    [_timer invalidate];
    _timer = nil;
}

- (void)pause:(NSNotification*)notification {
    [self closePopup];
    [self stopTimer];
}

- (void)resume:(NSNotification*)notification {
    [self startTimer];
}

- (void)systemColorsChanged:(NSNotification*)notification {
    gThemes.reset();
    _widget.needsDisplay = YES;
    _popup.needsDisplay = YES;
}

- (void)tick {
    _engine->tick(_panel || _tab == Tab::Sensors, _panel && _tab == Tab::Sensors);  // see Engine::tick
    [self refreshWidget];
    if (_panel) {
        [self placePopup];
        // The CPU and RAM tabs redraw once their process list arrives, a few ms later.
        if (![self refreshProcesses]) _popup.needsDisplay = YES;
    }
}

// The menu bar shows the widget of the last tab hovered. Every repaint also
// makes AppKit re-snapshot the item for its menu bar replicas, which costs far
// more than sampling, so jumps and colour changes repaint right away while
// small wiggles are coalesced to one repaint every couple of seconds.
- (void)refreshWidget {
    WidgetData next = widgetFor(_tab, *_engine, _fahrenheit);
    const WidgetChange change = compareWidgets(_widget->_data, next);
    const CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    if (change == WidgetChange::None || (change == WidgetChange::Minor && now - _paintedAt < kWiggleDelay)) return;
    const CGFloat width = widgetWidth(next, gThemes.forAppearance(_item.button.effectiveAppearance));
    if (_item.length != width) _item.length = width;
    _widget->_data = std::move(next);
    _widget.needsDisplay = YES;
    _paintedAt = now;
}

#pragma mark Popup

- (void)itemClicked:(NSStatusBarButton*)sender {
    NSEvent* event = NSApp.currentEvent;
    if (event.type == NSEventTypeRightMouseDown || (event.modifierFlags & NSEventModifierFlagControl)) {
        [self closePopup];
        [self showMenuInView:sender at:NSMakePoint(0, NSHeight(sender.bounds) + 6)];
    } else if (_panel) {
        [self closePopup];
    } else {
        [self openPopup];
    }
}

- (void)openPopup {
    [self readTemperatureUnit];
    if (_tab == Tab::Sensors) {
        _engine->sensors.sample();  // the SMC is only read while visible
    } else {
        _engine->sensors.sampleHeadline();  // for the Sensors tab's number
    }
    _popup = [[WTPopupView alloc] initWithFrame:NSMakeRect(0, 0, kPopupWidth, 100)];
    _popup->_app = self;
    _popup->_content.tab = _tab;
    _popup->_content.engine = _engine.get();
    _popup->_content.fahrenheit = _fahrenheit;

    // Tabs switch on touch, so open with the menu button (not a tab) right under
    // the icon: moving straight down into the popup doesn't flip tabs by accident.
    NSStatusBarButton* button = _item.button;
    const Theme& theme = gThemes.forAppearance(button.effectiveAppearance);
    const CGRect menu = renderPopup(nullptr, _popup->_content, {}, theme).menuButton;
    const NSRect anchor = [button.window convertRectToScreen:[button convertRect:button.bounds toView:nil]];
    const NSRect screen = (button.window.screen ?: NSScreen.mainScreen).frame;
    _popupX = std::round(NSMidX(anchor) - CGRectGetMidX(menu));
    _popupX = std::max(NSMinX(screen) + 6, std::min(_popupX, NSMaxX(screen) - kPopupWidth - 6));

    _panel = [[WTPanel alloc] initWithContentRect:NSMakeRect(0, 0, kPopupWidth, 100)
                                        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                          backing:NSBackingStoreBuffered
                                            defer:YES];
    _panel.releasedWhenClosed = NO;
    _panel.animationBehavior = NSWindowAnimationBehaviorNone;  // appear and vanish instantly
    _panel.depthLimit = NSWindowDepthTwentyfourBitRGB;
    _panel.opaque = NO;
    _panel.backgroundColor = NSColor.clearColor;
    _panel.hasShadow = YES;
    _panel.level = NSPopUpMenuWindowLevel;
    _panel.hidesOnDeactivate = NO;
    _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorTransient |
                                NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorIgnoresCycle;
    // The native popover material (blurred, adapts to light/dark) with rounded corners.
    NSVisualEffectView* material = [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(0, 0, kPopupWidth, 100)];
    material.material = NSVisualEffectMaterialPopover;
    material.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    material.state = NSVisualEffectStateActive;
    material.maskImage = roundedMask(kPopupRadius);
    _popup.frame = material.bounds;
    _popup.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [material addSubview:_popup];
    _panel.contentView = material;
    [self placePopup];
    [_panel orderFrontRegardless];
    [_panel display];
    [_panel invalidateShadow];

    if (!_clickMonitor) {
        // Clicks elsewhere close the popup. The monitor is installed once and
        // kept: AppKit leaks a Mach port every time a global monitor is removed.
        __weak WTApp* weakSelf = self;
        _clickMonitor = [NSEvent addGlobalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown
                                                               handler:^(NSEvent*) {
                                                                   [weakSelf closePopup];
                                                               }];
    }
    [self refreshProcesses];
}

- (void)selectTab:(Tab)tab {
    if (!_panel || tab == _tab) return;
    _tab = tab;
    [NSUserDefaults.standardUserDefaults setInteger:int(tab) forKey:kTabKey];
    if (tab == Tab::Sensors) _engine->sensors.sample();
    _popup->_content.tab = tab;
    _popup->_content.processes.clear();
    [self refreshWidget];
    [self placePopup];
    _popup.needsDisplay = YES;
    [self refreshProcesses];
}

- (void)placePopup {
    if (!_panel) return;
    const Theme& t = gThemes.forAppearance(_popup.effectiveAppearance);
    _popup->_layout = renderPopup(nullptr, _popup->_content, _popup->_hover, t);
    const CGFloat height = std::ceil(_popup->_layout.height);
    NSStatusBarButton* button = _item.button;
    const NSRect anchor = [button.window convertRectToScreen:[button convertRect:button.bounds toView:nil]];
    const NSRect frame = NSMakeRect(_popupX, NSMinY(anchor) - 6 - height, kPopupWidth, height);
    if (!NSEqualRects(frame, _panel.frame)) {
        [_panel setFrame:frame display:YES];
        [_panel invalidateShadow];
    }
}

- (void)closePopup {
    if (!_panel) return;
    [_panel close];  // frees the window server's buffers; orderOut alone keeps them
    _panel.contentView = nil;
    _panel = nil;
    _popup = nil;
    _directory.clear();
    // Hand the popup's freed memory back to the OS once AppKit has let go of it.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        malloc_zone_pressure_relief(nullptr, 0);
    });
}

#pragma mark Processes

// Samples the open tab's processes in the background and redraws the popup when
// they arrive. Returns NO if no new sample was started.
- (BOOL)refreshProcesses {
    const Tab tab = _tab;
    if (!_panel || (tab != Tab::Cpu && tab != Tab::Ram)) return NO;
    if (tab == Tab::Ram) [self refreshForeignSnapshot];
    if (_processBusy) return NO;
    _processBusy = YES;

    const size_t slots = processSlots(tab);
    const auto foreign = std::make_shared<const std::vector<ProcessUsage>>(_foreign);
    __weak WTApp* weakSelf = self;
    dispatch_async(_processQueue, ^{
        const auto rows = std::make_shared<const std::vector<ProcessUsage>>(
            tab == Tab::Cpu ? topCpuProcesses(slots) : topMemoryProcesses(slots, *foreign));
        dispatch_async(dispatch_get_main_queue(), ^{
            WTApp* app = weakSelf;
            if (!app) return;
            app->_processBusy = NO;
            if (!app->_panel) return;
            if (app->_tab == tab) {
                [app showProcesses:*rows];
            } else {
                [app refreshProcesses];  // the tab changed while we were busy
            }
        });
    });
    return YES;
}

// WindowServer & co. belong to other users; only setuid `top` can see their
// footprint and it is expensive, so it runs at most every 15 s, in the background.
- (void)refreshForeignSnapshot {
    if (_topBusy || CFAbsoluteTimeGetCurrent() - _foreignTime < kForeignRefresh) return;
    _topBusy = YES;
    __weak WTApp* weakSelf = self;
    dispatch_async(_topQueue, ^{
        const auto snapshot = std::make_shared<const std::vector<ProcessUsage>>(foreignMemorySnapshot(40));
        dispatch_async(dispatch_get_main_queue(), ^{
            WTApp* app = weakSelf;
            if (!app) return;
            app->_topBusy = NO;
            app->_foreign = *snapshot;
            app->_foreignTime = CFAbsoluteTimeGetCurrent();
            if (app->_panel && app->_tab == Tab::Ram) [app refreshProcesses];
        });
    });
}

- (void)showProcesses:(const std::vector<ProcessUsage>&)rows {
    const CGFloat scale = backingScale(_panel);
    std::vector<ProcessRow> display;
    display.reserve(rows.size());
    for (const ProcessUsage& row : rows) display.push_back(_directory.row(row, _tab, scale));
    _popup->_content.processes = std::move(display);
    _popup.needsDisplay = YES;
}

#pragma mark Menu

- (void)showMenuInView:(NSView*)view at:(NSPoint)point {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Watcher"];
    NSMenuItem* login = [menu addItemWithTitle:@"Launch at Login" action:@selector(toggleLaunchAtLogin:) keyEquivalent:@""];
    login.target = self;
    login.state = SMAppService.mainAppService.status == SMAppServiceStatusEnabled ? NSControlStateValueOn
                                                                                  : NSControlStateValueOff;
    [menu addItemWithTitle:@"Open Activity Monitor" action:@selector(openActivityMonitor:) keyEquivalent:@""].target = self;
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItemWithTitle:@"About Watcher" action:@selector(showAbout:) keyEquivalent:@""].target = self;
    [menu addItemWithTitle:@"Quit Watcher" action:@selector(terminate:) keyEquivalent:@"q"].target = NSApp;
    [menu popUpMenuPositioningItem:nil atLocation:point inView:view];
}

- (void)toggleLaunchAtLogin:(id)sender {
    SMAppService* service = SMAppService.mainAppService;
    NSError* error = nil;
    const BOOL enabled = service.status == SMAppServiceStatusEnabled;
    if (!(enabled ? [service unregisterAndReturnError:&error] : [service registerAndReturnError:&error])) {
        NSLog(@"Watcher: could not change Launch at Login: %@", error);
    }
}

- (void)openActivityMonitor:(id)sender {
    [self closePopup];
    NSURL* url = [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier:@"com.apple.ActivityMonitor"];
    if (url) {
        [NSWorkspace.sharedWorkspace openApplicationAtURL:url
                                            configuration:NSWorkspaceOpenConfiguration.configuration
                                        completionHandler:nil];
    }
}

- (void)showAbout:(id)sender {
    [self closePopup];
    [NSApp activate];
    [NSApp orderFrontStandardAboutPanel:nil];
}

@end

int main(int argc, const char* argv[]) {
    if (argc >= 3 && strcmp(argv[1], "--snapshot") == 0) return runSnapshot(argv[2], argc >= 4 ? atoi(argv[3]) : 3);
    @autoreleasepool {
        NSApplication* app = NSApplication.sharedApplication;
        app.activationPolicy = NSApplicationActivationPolicyAccessory;
        static WTApp* delegate = [WTApp new];  // NSApplication holds its delegate weakly
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
