# Blaise: Claws Mail rebuilt on Native.
ROOT := $(CURDIR)
BUILD ?= $(ROOT)/build
BIN ?= $(ROOT)/bin
MODE ?= release
CXX := g++
CC := gcc
AR := ar
NATIVE_ROOT ?= $(if $(wildcard $(ROOT)/../native/include/native.h),$(abspath $(ROOT)/../native),$(ROOT)/build/deps/native)
VERSION ?= 0.1.0
NATIVE_BUILD := $(BUILD)/native-$(MODE)
NATIVE_ARCHIVE := $(NATIVE_BUILD)/native/lib/native/libnative.a
NATIVE_RC := $(NATIVE_BUILD)/native-rc/rc
NATIVE_LDLIBS := -L$(BUILD)/sdk/usr/lib/x86_64-linux-gnu -lSDL2 -l:libSDL2_ttf-2.0.so.0 -lfontconfig -lSM -lICE -lX11 -lXext -lpng -lz -ljpeg -pthread -ldl
OBJROOT := $(BUILD)/$(MODE)
LIB_NAMES := view compose filter addr mail net prefs mime util
INCLUDES := -I$(ROOT)/include $(foreach n,$(LIB_NAMES),-I$(ROOT)/lib/$(n)/include)
CXXFLAGS := -std=c++20 -Wall -Wextra -pedantic $(INCLUDES) -pthread -DBLAISE_VERSION='"$(VERSION)"'
# TLS is the one library the core may use; see docs/notes/TLS.md.
TLS ?= $(if $(wildcard /usr/include/openssl/ssl.h),openssl,none)
ifeq ($(TLS),openssl)
CXXFLAGS += -DBLAISE_TLS_OPENSSL
TLS_LDLIBS := -lssl -lcrypto
endif
# ASan cannot start on hosts that preload a security agent; see README.
SANITIZERS ?= $(if $(wildcard /etc/ld.so.preload),undefined,address,undefined)
# Native's Debug build adds ASan itself, so both must agree or linking fails.
NATIVE_CHECKS := $(if $(findstring address,$(SANITIZERS)),ON,OFF)
ifeq ($(MODE),debug)
CXXFLAGS += -O1 -g -fno-omit-frame-pointer -fsanitize=$(SANITIZERS) -fno-pie
LDFLAGS := -fsanitize=$(SANITIZERS) -no-pie -pthread
SUFFIX := -debug
NATIVE_CONFIG := Debug
else
CXXFLAGS += -O2 -DNDEBUG
LDFLAGS := -pthread
SUFFIX :=
NATIVE_CONFIG := Release
endif
LINK_LIBS := $(foreach n,$(LIB_NAMES),$(OBJROOT)/lib$(n).a)
TARGET := $(BIN)/blaise$(SUFFIX)
HEADLESS_TARGET := $(BIN)/blaise-headless$(SUFFIX)
export ROOT BUILD BIN MODE CXX CC AR OBJROOT CXXFLAGS LDFLAGS TLS_LDLIBS
export LINK_LIBS TARGET HEADLESS_TARGET
export NATIVE_ROOT NATIVE_ARCHIVE NATIVE_LDLIBS SUFFIX NATIVE_CHECKS
export NATIVE_RC

.PHONY: all release debug libs app native headless test docker-test run-tests run clean ui-test ui-build live-test claws-test platform-test list-platforms resources $(LIB_NAMES)
all: app
libs: $(LIB_NAMES)
$(LIB_NAMES):
	@mkdir -p "$(OBJROOT)"
	+flock "$(OBJROOT)/$@.lock" $(MAKE) --no-print-directory -f $(ROOT)/lib/library.mk NAME=$@
native:
	@mkdir -p "$(BUILD)"
	+flock "$(BUILD)/native-$(MODE).lock" sh src/ui/build_native.sh \
	    "$(NATIVE_ROOT)" "$(NATIVE_BUILD)" "$(NATIVE_CONFIG)"
app: libs native
	+flock "$(OBJROOT)/src.lock" $(MAKE) --no-print-directory -C src
resources: native
	@mkdir -p "$(OBJROOT)"
	+flock "$(OBJROOT)/src.lock" $(MAKE) --no-print-directory -C src resources
headless: libs
	+flock "$(OBJROOT)/src.lock" $(MAKE) --no-print-directory -C src headless
release:
	+$(MAKE) MODE=release all
debug:
	+$(MAKE) MODE=debug all
test:
	+$(MAKE) MODE=debug run-tests
docker-test:
	docker run --rm -u $$(id -u):$$(id -g) -v "$(ROOT):$(ROOT)" -w "$(ROOT)" \
	    wischner/gcc-x86_64-linux-sdl:latest \
	    make -j8 test BUILD=$(ROOT)/build/docker BIN=$(ROOT)/build/docker/bin \
	    SANITIZERS=address,undefined
run-tests: libs
	+flock "$(OBJROOT)/tests.lock" $(MAKE) --no-print-directory -C tests run
run: all
	$(TARGET)
ui-build: app
	+flock "$(OBJROOT)/tests.lock" $(MAKE) --no-print-directory -C tests ui-build
ui-test: app
	+flock "$(OBJROOT)/tests.lock" $(MAKE) --no-print-directory -C tests ui
live-test: libs
	+flock "$(OBJROOT)/tests.lock" $(MAKE) --no-print-directory -C tests live
claws-test: libs
	+flock "$(OBJROOT)/tests.lock" $(MAKE) --no-print-directory -C tests claws
# Build and test one backend in its SDK; PLATFORM=all runs every one.
PLATFORM ?= all
platform-test:
	python3 -B scripts/build/platforms.py $(PLATFORM)
list-platforms:
	python3 -B scripts/build/platforms.py --list
clean:
	rm -rf -- "$(BUILD)" "$(BIN)"
