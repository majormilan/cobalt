# Makefile for COBALT

CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude -Iruntime  # Debug build; add -O2 for release
DEPFLAGS = -MMD -MP                    # Auto-generate .d dependency files
LDFLAGS =  # Add libs later, e.g., -lm

SRCDIR = src
BINDIR = bin
OBJDIR = obj

# Find all .c files
SOURCES = $(shell find $(SRCDIR) -name '*.c')
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
DEPS    = $(OBJECTS:.o=.d)

EXEC = $(BINDIR)/cobalt

# Runtime target selection: each target triple maps to an arch layer
# (runtime/<arch>/common), an OS layer (runtime/<arch>/<os>) and an ABI.
TARGET ?= x86_64-linux-elf

ifeq ($(TARGET),x86_64-linux-elf)
    RT_ARCH  = x86_64
    RT_OS    = linux
    RT_ABI   = SYSV
    NASM_FMT = elf64
else
    $(error Unsupported TARGET '$(TARGET)'. Supported: x86_64-linux-elf)
endif

RT_SRCDIR  = runtime/$(RT_ARCH)
RT_OBJDIR  = $(OBJDIR)/runtime/$(TARGET)
RT_SOURCES = $(wildcard $(RT_SRCDIR)/common/*.asm) $(wildcard $(RT_SRCDIR)/$(RT_OS)/*.asm)
RT_OBJECTS = $(RT_SOURCES:$(RT_SRCDIR)/%.asm=$(RT_OBJDIR)/%.o)
RT_LIB     = $(RT_OBJDIR)/libcobrt.a
NASMFLAGS  = -f $(NASM_FMT) -D ABI_$(RT_ABI) -I $(RT_SRCDIR)/

all: $(EXEC) $(RT_LIB)

$(EXEC): $(OBJECTS) | $(BINDIR)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Include auto-generated dependency files (silently ignore if missing)
-include $(DEPS)
-include $(RT_OBJECTS:.o=.d)

runtime: $(RT_LIB)

$(RT_LIB): $(RT_OBJECTS)
	rm -f $@
	ar rcs $@ $^

$(RT_OBJDIR)/%.o: $(RT_SRCDIR)/%.asm
	@mkdir -p $(@D)
	nasm $(NASMFLAGS) -MD $(@:.o=.d) -MP $< -o $@

# Verify the runtime exports every routine listed in runtime/routines.def
check-runtime: $(RT_LIB)
	@missing=0; \
	for sym in $$(sed -n 's/^RUNTIME_ROUTINE(\([a-z0-9_]*\))$$/\1/p' runtime/routines.def); do \
		if ! nm -g --defined-only $(RT_LIB) | grep -q " T $$sym$$"; then \
			echo "  MISSING  $$sym"; missing=$$((missing+1)); \
		fi; \
	done; \
	[ $$missing -eq 0 ] && echo "Runtime $(TARGET) exports all contract routines."

$(BINDIR):
	mkdir -p $(BINDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)
	@mkdir -p $(OBJDIR)/lexer $(OBJDIR)/parser $(OBJDIR)/backend $(OBJDIR)/ir $(OBJDIR)/sema

clean:
	rm -rf $(OBJDIR) $(BINDIR)

# A tests/<name>.xfail file marks a known failure: it is reported as XFAIL
# and does not fail the run. If such a test starts passing (XPASS), the run
# fails so the marker gets removed.
test: $(EXEC) check-runtime
	@echo "Running tests ($(TARGET))..."
	@passed=0; failed=0; xfailed=0; skipped=0; \
	for cbl in tests/*.cbl; do \
		base=$$(basename $$cbl .cbl); \
		if [ ! -f "tests/$$base.expected" ]; then \
			echo "  SKIP  $$base (no .expected)"; skipped=$$((skipped+1)); continue; \
		fi; \
		if TARGET=$(TARGET) bash test_runner.sh $$cbl > /dev/null 2>&1; then ok=1; else ok=0; fi; \
		if [ -f "tests/$$base.xfail" ]; then \
			if [ $$ok -eq 1 ]; then \
				echo "  XPASS $$base (remove tests/$$base.xfail)"; failed=$$((failed+1)); \
			else \
				echo "  XFAIL $$base"; xfailed=$$((xfailed+1)); \
			fi; \
		elif [ $$ok -eq 1 ]; then \
			echo "  PASS  $$base"; passed=$$((passed+1)); \
		else \
			echo "  FAIL  $$base"; failed=$$((failed+1)); \
		fi; \
	done; \
	echo ""; \
	echo "Results: $$passed passed, $$failed failed, $$xfailed expected failures, $$skipped skipped"; \
	[ $$failed -eq 0 ]

.PHONY: all clean test runtime check-runtime
