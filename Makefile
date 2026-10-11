APP       := Watcher
VERSION   := 1.1.0
BUILD     := build
BUNDLE    := $(BUILD)/$(APP).app
BINARY    := $(BUNDLE)/Contents/MacOS/$(APP)
PREFIX    ?= /Applications

CXX       := clang++
ARCH      ?= arm64
MIN_MACOS := 27.0

CPP_SRC   := $(wildcard src/core/*.cpp src/ui/*.cpp)
MM_SRC    := $(wildcard src/app/*.mm)
OBJ       := $(CPP_SRC:src/%.cpp=$(BUILD)/obj/%.o) $(MM_SRC:src/%.mm=$(BUILD)/obj/%.o)

TARGET    := -arch $(ARCH) -mmacosx-version-min=$(MIN_MACOS)
# Size first: -Os, link-time optimisation, no C++ exceptions or RTTI (unused).
CXXFLAGS  += $(TARGET) -std=c++20 -Os -flto -fno-exceptions -fno-rtti -Wall -Wextra -Wshadow -fvisibility=hidden -MMD -MP
OBJCFLAGS := -fobjc-arc
LDFLAGS   += $(TARGET) -flto -dead_strip
LIBS      := -framework AppKit -framework IOKit -framework ServiceManagement -framework ImageIO -framework QuartzCore

ICON      := resources/$(APP).icns
RESOURCES := $(BUNDLE)/Contents/Info.plist $(if $(wildcard $(ICON)),$(BUNDLE)/Contents/Resources/$(APP).icns)

.PHONY: all app run install uninstall snapshot icon clean

all: app

app: $(BUILD)/.signed

# Ad-hoc sign only when something in the bundle changed.
$(BUILD)/.signed: $(BINARY) $(RESOURCES)
	@codesign --force --sign - --timestamp=none $(BUNDLE)
	@touch $@
	@echo "Built $(BUNDLE) ($$(du -sh $(BUNDLE) | cut -f1))"

$(BINARY): $(OBJ)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)
	@strip -x $@

$(BUNDLE)/Contents/Info.plist: resources/Info.plist Makefile
	@mkdir -p $(@D)
	@sed 's/__VERSION__/$(VERSION)/g' $< > $@

$(BUNDLE)/Contents/Resources/$(APP).icns: $(ICON)
	@mkdir -p $(@D)
	@cp $< $@

$(BUILD)/obj/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: src/%.mm
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(OBJCFLAGS) -c $< -o $@

# Waits for a running copy to quit first: `open` fails while it is still exiting.
STOP      := pkill -x $(APP) && while pgrep -x $(APP) >/dev/null; do sleep 0.1; done; true

run: app
	@$(STOP)
	@open $(BUNDLE)

install: app
	@$(STOP)
	@rm -rf "$(PREFIX)/$(APP).app"
	@cp -R $(BUNDLE) "$(PREFIX)/"
	@open "$(PREFIX)/$(APP).app"
	@echo "Installed to $(PREFIX)/$(APP).app"

uninstall:
	@$(STOP)
	@rm -rf "$(PREFIX)/$(APP).app"

# Renders every popup and the menu bar to docs/ after sampling for SAMPLE seconds.
SAMPLE ?= 3
snapshot: app
	@$(BINARY) --snapshot docs $(SAMPLE)

icon: tools/icon.cpp
	@rm -rf $(BUILD)/icon.iconset && mkdir -p $(BUILD)/icon.iconset
	$(CXX) $(TARGET) -std=c++20 -O2 $< -o $(BUILD)/icon -framework CoreGraphics -framework ImageIO -framework CoreFoundation
	@$(BUILD)/icon $(BUILD)/icon.iconset
	@iconutil -c icns $(BUILD)/icon.iconset -o $(ICON)
	@echo "Wrote $(ICON)"

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)
