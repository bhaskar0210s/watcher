#pragma once

// AppKit glue shared by the app and the snapshot renderer (Objective-C++ only).

#import <AppKit/AppKit.h>

#include "../core/processes.hpp"
#include "../ui/popup.hpp"
#include "../ui/theme.hpp"

#include <memory>
#include <unordered_map>

namespace watcher {

// Resolves system colors and fonts once per appearance.
class Themes {
public:
    const Theme& get(bool dark);
    const Theme& forAppearance(NSAppearance* appearance);
    void reset();

private:
    std::unique_ptr<Theme> dark_, light_;
};

extern Themes gThemes;

// Display names and small icons for process rows, cached while a popup is open.
class ProcessDirectory {
public:
    ProcessRow row(const ProcessUsage& usage, Tab tab, CGFloat scale);
    void clear();

private:
    struct Identity {
        std::string name;
        CFRef<CGImageRef> icon;
    };
    Identity lookup(pid_t pid, CGFloat scale);

    std::unordered_map<pid_t, Identity> cache_;
    CFRef<CGImageRef> genericIcon_;
};

int runSnapshot(const char* directory, int seconds);

}  // namespace watcher
