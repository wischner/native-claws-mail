SRCDIR := $(ROOT)/src
OBJDIR := $(OBJROOT)/src
APP_SRC := $(wildcard $(SRCDIR)/app/*.cpp)
APP_OBJ := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(APP_SRC))
UI_SRC := $(wildcard $(SRCDIR)/ui/*.cpp)
UI_OBJ := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(UI_SRC))
RESOURCE_CPP := $(OBJDIR)/ui/resources.cpp
RESOURCE_OBJ := $(OBJDIR)/ui/resources.o
RESOURCE_INPUTS := $(SRCDIR)/resources/blaise.json $(wildcard $(SRCDIR)/resources/icons/*/*.png)
.PHONY: all headless
all: $(TARGET)
headless: $(HEADLESS_TARGET)
$(TARGET): $(OBJDIR)/main.o $(APP_OBJ) $(UI_OBJ) $(RESOURCE_OBJ) $(LINK_LIBS) $(NATIVE_ARCHIVE)
	@mkdir -p $(dir $@)
	$(CXX) $^ $(LDFLAGS) $(NATIVE_LDLIBS) $(TLS_LDLIBS) -o $@
$(HEADLESS_TARGET): $(OBJDIR)/main_headless.o $(APP_OBJ) $(LINK_LIBS)
	@mkdir -p $(dir $@)
	$(CXX) $^ $(LDFLAGS) $(TLS_LDLIBS) -o $@
.PHONY: resources
resources: $(RESOURCE_CPP) $(OBJDIR)/blaise.resources
$(RESOURCE_CPP) $(OBJDIR)/blaise.resources &: $(RESOURCE_INPUTS) $(NATIVE_RC)
	@mkdir -p $(dir $@) $(dir $(RESOURCE_CPP))
	$(NATIVE_RC) $(SRCDIR)/resources/blaise.json -o $(OBJDIR)/blaise.resources --cpp $(RESOURCE_CPP)
$(RESOURCE_OBJ): $(RESOURCE_CPP) $(SRCDIR)/Makefile $(ROOT)/Makefile
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJDIR)/main_headless.o: $(SRCDIR)/main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -DBLAISE_HEADLESS -MMD -MP -c $< -o $@
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(SRCDIR)/Makefile $(ROOT)/Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -isystem $(NATIVE_ROOT)/include -I$(SRCDIR)/ui -I$(OBJDIR) -MMD -MP -c $< -o $@
-include $(APP_OBJ:.o=.d) $(UI_OBJ:.o=.d) $(OBJDIR)/main.d $(OBJDIR)/main_headless.d
