# Blaise: Claws Mail rebuilt on Native.
ROOT := $(CURDIR)
# Compilation runs in the SDL2 SDK image, so the host needs only Git, make
# and Docker. BLAISE_IN_DOCKER marks the make inside the container.
# The image is Native's SDL2 SDK plus OpenSSL, built here from
# scripts/build/docker/linux-sdl and tagged with a hash of its Dockerfile.
SDK_CONTEXT := $(ROOT)/scripts/build/docker/linux-sdl
SDK_DEFAULT := blaise-linux-sdl:$(shell sha256sum < "$(SDK_CONTEXT)/Dockerfile" | cut -c1-12)
DOCKER_IMAGE ?= $(SDK_DEFAULT)
BUILD ?= $(ROOT)/build/sdl2
BIN ?= $(ROOT)/bin
MODE ?= release
CXX := g++
CC := gcc
AR := ar
# Native is fetched from GitHub on first use; NATIVE_ROOT=/path uses a checkout.
NATIVE_DEFAULT := $(ROOT)/build/deps/native
NATIVE_ROOT ?= $(NATIVE_DEFAULT)
VERSION ?= 0.1.0
NATIVE_BUILD := $(BUILD)/native-$(MODE)
NATIVE_ARCHIVE := $(NATIVE_BUILD)/native/lib/native/libnative.a
NATIVE_RC := $(NATIVE_BUILD)/native-rc/rc
NATIVE_LDLIBS := -lSDL2 -l:libSDL2_ttf-2.0.so.0 -lfontconfig -lSM -lICE -lX11 -lXext -lpng -lz -ljpeg -pthread -ldl
OBJROOT := $(BUILD)/$(MODE)
LIB_NAMES := view compose filter addr mail net prefs mime util
INCLUDES := -I$(ROOT)/include $(foreach n,$(LIB_NAMES),-I$(ROOT)/lib/$(n)/include)
CXXFLAGS := -std=c++20 -Wall -Wextra -pedantic $(INCLUDES) -pthread -DBLAISE_VERSION='"$(VERSION)"'
# TLS is the one library the core may use; see docs/notes/TLS.md. The SDK
# image has OpenSSL; TLS=none builds without it.
TLS ?= openssl
ifeq ($(TLS),openssl)
CXXFLAGS += -DBLAISE_TLS_OPENSSL
TLS_LDLIBS := -lssl -lcrypto
endif
# Builds and runs happen in the container, so a host preload cannot stop ASan.
SANITIZERS ?= address,undefined
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

.PHONY: all release debug libs app native headless resources test docker-test \
    run-tests run clean ui-test ui-build live-build live-test claws-build \
    claws-test platform-test list-platforms update-native sdk-image native-src \
    dist $(LIB_NAMES)
CONTAINER_GOALS := all release debug libs app native headless resources test \
    run-tests ui-build ui-test live-build claws-build $(LIB_NAMES)
DOCKER_RUN = docker run --rm --init -u $$(id -u):$$(id -g) -e HOME=/tmp \
    -e BLAISE_IN_DOCKER=1 -v "$(ROOT):$(ROOT)" \
    $(if $(filter $(ROOT)/%,$(NATIVE_ROOT)),,-v "$(NATIVE_ROOT):$(NATIVE_ROOT)") \
    -w "$(ROOT)" $(DOCKER_IMAGE)

ifndef BLAISE_IN_DOCKER
# Forward each build goal into the SDK image; command-line variables follow.
JOBS = $(or $(filter -j%,$(MAKEFLAGS)),-j$(shell nproc))
$(CONTAINER_GOALS): sdk-image native-src
	$(DOCKER_RUN) make --no-print-directory $(JOBS) $@ $(MAKEOVERRIDES)
# Build the SDK image unless it exists; an image named on the command line
# (DOCKER_IMAGE=...) is used as it is.
sdk-image:
	$(if $(filter $(SDK_DEFAULT),$(DOCKER_IMAGE)),@docker image inspect $(DOCKER_IMAGE) >/dev/null 2>&1 \
	    || docker build -t $(DOCKER_IMAGE) "$(SDK_CONTEXT)")
native-src:
	$(if $(filter $(NATIVE_DEFAULT),$(NATIVE_ROOT)),@sh scripts/build/fetch_native.sh >/dev/null)
else
all: app
libs: $(LIB_NAMES)
$(LIB_NAMES):
	@mkdir -p "$(OBJROOT)"
	+flock "$(OBJROOT)/$@.lock" $(MAKE) --no-print-directory -f $(ROOT)/lib/library.mk NAME=$@
native:
	@mkdir -p "$(BUILD)"
	$(if $(filter $(NATIVE_DEFAULT),$(NATIVE_ROOT)),@sh scripts/build/fetch_native.sh >/dev/null)
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
run-tests: libs
	+flock "$(OBJROOT)/tests.lock" $(MAKE) --no-print-directory -C tests run
ui-build: app
	+flock "$(OBJROOT)/tests.lock" $(MAKE) --no-print-directory -C tests ui-build
ui-test: app
	+flock "$(OBJROOT)/tests.lock" $(MAKE) --no-print-directory -C tests ui
live-build: libs
	+flock "$(OBJROOT)/tests.lock" $(MAKE) --no-print-directory -C tests live-build
claws-build: libs
	+flock "$(OBJROOT)/tests.lock" $(MAKE) --no-print-directory -C tests claws-build
endif

# Host-side goals.
# The live and Claws tests start servers with Docker on the host; the test
# programs themselves run in the SDK image (see scripts/test/*.sh).
live-test: live-build
	BLAISE_SDK_IMAGE=$(DOCKER_IMAGE) bash scripts/test/live.sh "$(OBJROOT)/tests/test_live"
claws-test: claws-build
	BLAISE_SDK_IMAGE=$(DOCKER_IMAGE) bash scripts/test/claws.sh "$(OBJROOT)/tests/write_claws_folder"
# Move build/deps/native to the newest commit of NATIVE_REF.
update-native:
	sh scripts/build/fetch_native.sh --update >/dev/null
# Kept for old scripts: make test already runs ASan and UBSan in Docker.
docker-test: test
# The SDL2 image draws on the host desktop through its X11 socket.
run: all
	python3 -B scripts/debug/linux.py linux-sdl2 $(TARGET)
# Build and test one backend in its SDK; PLATFORM=all runs every one.
PLATFORM ?= all
platform-test:
	python3 -B scripts/build/platforms.py $(PLATFORM)
list-platforms:
	python3 -B scripts/build/platforms.py --list
# The release zip for one backend, as CI makes it: dist/blaise-VERSION-PLATFORM.zip.
dist:
	python3 -B scripts/build/package.py $(if $(filter all,$(PLATFORM)),linux-sdl2,$(PLATFORM)) --version $(VERSION)
clean:
	rm -rf -- "$(ROOT)/build" "$(BIN)"
