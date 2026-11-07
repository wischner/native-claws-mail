#
# Builds one lib/<NAME> into lib<NAME>.a under $(OBJROOT). The top level
# makefile exports the toolchain and runs this once per library, so the
# nine libraries share one recipe instead of nine copies.
#
# GPL 3.0 or later (see: LICENSE)
# Copyright (C) 2026 tomaz stih
#

SRCDIR  := $(ROOT)/lib/$(NAME)
OBJDIR  := $(OBJROOT)/$(NAME)

SOURCES := $(wildcard $(SRCDIR)/*.cpp)
OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))
DEPS    := $(OBJECTS:.o=.d)

LIBRARY := $(OBJROOT)/lib$(NAME).a

.PHONY: all

all: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	@mkdir -p $(dir $@)
	@rm -f $@
	@$(AR) rcs $@ $^
	@echo "  AR      $(notdir $@)"

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
	@echo "  CXX     $(NAME)/$(notdir $<)"

-include $(DEPS)
