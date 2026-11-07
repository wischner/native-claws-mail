#
# Sanitized unit tests: every tests/unit/test_*.cpp is one binary.
#
# GPL 3.0 or later (see: LICENSE)
# Copyright (C) 2026 tomaz stih
#

SRCDIR   := $(ROOT)/tests/unit
OBJDIR   := $(OBJROOT)/tests
SOURCES  := $(wildcard $(SRCDIR)/test_*.cpp)
BINARIES := $(patsubst $(SRCDIR)/test_%.cpp,$(OBJDIR)/test_%,$(SOURCES))
DEPS     := $(BINARIES:=.d)

UI_TEST  := $(OBJDIR)/test_desktop
UI_SOURCES := $(wildcard $(ROOT)/tests/ui/test_*.cpp)
UI_OBJ   := $(wildcard $(OBJROOT)/src/ui/*.o) $(filter-out %/main.o %/main_headless.o,$(wildcard $(OBJROOT)/src/app/*.o))

.PHONY: all run clean ui
all: $(BINARIES)

# Links the real window; the display it needs comes from native alone.
$(UI_TEST): $(UI_SOURCES) $(wildcard $(ROOT)/tests/ui/*.h) $(UI_OBJ) $(LINK_LIBS) $(NATIVE_ARCHIVE)
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -isystem $(NATIVE_ROOT)/include -I$(ROOT)/src/ui \
	    -I$(ROOT)/tests/unit -DFIXTURES='"$(ROOT)/tests/fixtures"' -MMD -MP \
	    $(UI_SOURCES) $(UI_OBJ) $(LINK_LIBS) $(NATIVE_ARCHIVE) $(LDFLAGS) \
	    $(NATIVE_LDLIBS) $(TLS_LDLIBS) -Wl,-rpath,'$$ORIGIN/../../sdk/usr/lib/x86_64-linux-gnu' \
	    -o $@
	@echo "  TEST    $(notdir $@)"

LIVE_TEST := $(OBJDIR)/test_live

$(LIVE_TEST): $(ROOT)/tests/live/test_live.cpp $(LINK_LIBS) $(APP_SOURCES)
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -I$(SRCDIR) -MMD -MP $< $(APP_SOURCES) \
	    $(LINK_LIBS) $(LINK_LIBS) $(LDFLAGS) $(TLS_LDLIBS) -o $@
	@echo "  TEST    $(notdir $@)"

.PHONY: live
live: $(LIVE_TEST)
	bash $(ROOT)/scripts/test/live.sh $(LIVE_TEST)

CLAWS_WRITER := $(OBJDIR)/write_claws_folder

$(CLAWS_WRITER): $(ROOT)/tests/live/write_claws_folder.cpp $(LINK_LIBS)
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -MMD -MP $< $(LINK_LIBS) $(LINK_LIBS) $(LDFLAGS) \
	    $(TLS_LDLIBS) -o $@

.PHONY: claws
claws: $(CLAWS_WRITER)
	bash $(ROOT)/scripts/test/claws.sh $(CLAWS_WRITER)

.PHONY: ui-build
ui-build: $(UI_TEST)

ui: $(UI_TEST)
	SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software $(UI_TEST)

APP_SOURCES := $(wildcard $(ROOT)/src/app/*.cpp)

$(OBJDIR)/test_%: $(SRCDIR)/test_%.cpp $(LINK_LIBS) $(APP_SOURCES)
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -I$(SRCDIR) -DFIXTURES='"$(ROOT)/tests/fixtures"' \
	    -MMD -MP $< $(APP_SOURCES) $(LINK_LIBS) $(LINK_LIBS) $(LDFLAGS) \
	    $(TLS_LDLIBS) -o $@
	@echo "  TEST    $(notdir $@)"

run: $(BINARIES)
	@failed=0; \
	for suite in $(BINARIES); do \
	    $$suite || failed=1; \
	done; \
	if [ $$failed -eq 0 ]; then \
	    echo "=== all test suites passed ==="; \
	else \
	    echo "=== TEST FAILURES ==="; \
	fi; \
	exit $$failed

clean:
	@rm -rf $(OBJDIR)

-include $(DEPS)
