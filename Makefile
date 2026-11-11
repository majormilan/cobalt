# Makefile for COBALT

CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude -Iruntime  # Debug build; add -O2 for release
DEPFLAGS = -MMD -MP                    # Auto-generate .d dependency files
CFLAGS += -include compat.h            # what a host's C library lacks (include/compat.h)
LDFLAGS =  # Add libs later, e.g., -lm

SRCDIR = src
BINDIR = bin
OBJDIR = obj

# Find all .c files
SOURCES = $(shell find $(SRCDIR) -name '*.c')
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
DEPS    = $(OBJECTS:.o=.d)

EXEC = $(BINDIR)/cobalt

# cobalt is built in two stages. The first stage (no runtime inside) assembles
# the runtime with its built-in assembler (`cobalt as`); the runtime objects
# are then embedded in the final cobalt, which assembles and links programs
# by itself. Neither stage needs nasm, ld or ar.
STAGE0 = $(OBJDIR)/cobalt-stage0

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
# Assembler for the runtime: the first-stage cobalt, or nasm (make ASSEMBLER=nasm)
ASSEMBLER ?= $(STAGE0) as
ifeq ($(ASSEMBLER),$(STAGE0) as)
    RT_ASM_DEP = $(STAGE0)
endif
# The runtime objects embedded in cobalt: a table of {data, size} under
# cobalt_runtime_<target>, C source that `cobalt embed` writes from the
# objects (so any C compiler builds cobalt). Every target's runtime is
# embedded, so one cobalt builds for any of them; the Windows runtime differs
# only in its OS layer (runtime/x86_64/windows).
RT_SYMBOL   = cobalt_runtime_$(subst -,_,$(TARGET))
RT_BLOB_C   = $(RT_OBJDIR)/runtime_blob.c
RT_BLOB_OBJ = $(RT_OBJDIR)/runtime_blob.o
WIN_TARGET      = x86_64-pc-windows-msvc
WIN_RT_OBJDIR   = $(OBJDIR)/runtime/$(WIN_TARGET)
WIN_RT_SOURCES  = $(wildcard $(RT_SRCDIR)/common/*.asm) $(wildcard $(RT_SRCDIR)/windows/*.asm)
WIN_RT_OBJECTS  = $(WIN_RT_SOURCES:$(RT_SRCDIR)/%.asm=$(WIN_RT_OBJDIR)/%.o)
WIN_RT_BLOB_C   = $(WIN_RT_OBJDIR)/runtime_blob.c
WIN_RT_BLOB_OBJ = $(WIN_RT_OBJDIR)/runtime_blob.o
RT_BLOBS        = $(RT_BLOB_OBJ) $(WIN_RT_BLOB_OBJ)

# The first stage: cobalt without runtimes (main.c built with COBALT_STAGE0)
STAGE0_OBJECTS = $(filter-out $(OBJDIR)/main.o,$(OBJECTS)) $(OBJDIR)/main-stage0.o

all: $(EXEC)

$(STAGE0): $(STAGE0_OBJECTS)
	$(CC) $(STAGE0_OBJECTS) $(LDFLAGS) -o $@

$(OBJDIR)/main-stage0.o: $(SRCDIR)/main.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -DCOBALT_STAGE0 -c $< -o $@

$(EXEC): $(OBJECTS) $(RT_BLOBS) | $(BINDIR)
	$(CC) $(OBJECTS) $(RT_BLOBS) $(LDFLAGS) -o $@

$(RT_BLOB_C): $(RT_OBJECTS) $(STAGE0)
	$(STAGE0) embed $(RT_SYMBOL) $(RT_OBJECTS) > $@

$(WIN_RT_BLOB_C): $(WIN_RT_OBJECTS) $(STAGE0)
	$(STAGE0) embed cobalt_runtime_x86_64_pc_windows_msvc $(WIN_RT_OBJECTS) > $@

%/runtime_blob.o: %/runtime_blob.c
	$(CC) -c $< -o $@

# cobalt for Windows (bin/cobalt.exe), cross-built with MinGW-w64 from the
# same sources and runtimes
MINGW_CC      ?= x86_64-w64-mingw32-gcc
MINGW_OBJDIR   = $(OBJDIR)/mingw
MINGW_OBJECTS  = $(SOURCES:$(SRCDIR)/%.c=$(MINGW_OBJDIR)/%.o)
windows: $(BINDIR)/cobalt.exe

$(BINDIR)/cobalt.exe: $(MINGW_OBJECTS) $(RT_BLOB_C) $(WIN_RT_BLOB_C) | $(BINDIR)
	$(MINGW_CC) -O2 $(MINGW_OBJECTS) $(RT_BLOB_C) $(WIN_RT_BLOB_C) -o $@

$(MINGW_OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(MINGW_CC) -O2 -Wall -Iinclude -Iruntime -include compat.h $(DEPFLAGS) -c $< -o $@

-include $(MINGW_OBJECTS:.o=.d)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Include auto-generated dependency files (silently ignore if missing)
-include $(DEPS)
-include $(RT_OBJECTS:.o=.d)
-include $(WIN_RT_OBJECTS:.o=.d)

runtime: $(RT_OBJECTS)

# The runtime as an archive, for linking with ld (optional; needs ar)
runtime-lib: $(RT_LIB)

$(RT_LIB): $(RT_OBJECTS)
	rm -f $@
	ar rcs $@ $^

$(RT_OBJDIR)/%.o: $(RT_SRCDIR)/%.asm $(RT_ASM_DEP)
	@mkdir -p $(@D)
	$(ASSEMBLER) $(NASMFLAGS) -MD $(@:.o=.d) -MP $< -o $@

# The Windows runtime: the same code (System V inside), its own OS layer
$(WIN_RT_OBJDIR)/%.o: $(RT_SRCDIR)/%.asm $(RT_ASM_DEP)
	@mkdir -p $(@D)
	$(ASSEMBLER) $(NASMFLAGS) -MD $(@:.o=.d) -MP $< -o $@

# Verify the runtime defines every routine listed in runtime/routines.def
# (declared global in its sources; linking checks the definitions)
check-runtime: $(RT_OBJECTS)
	@missing=0; \
	for sym in $$(sed -n 's/^RUNTIME_ROUTINE(\([a-z0-9_]*\))$$/\1/p' runtime/routines.def); do \
		if ! grep -qE "^global $$sym$$" $(RT_SOURCES); then \
			echo "  MISSING  $$sym"; missing=$$((missing+1)); \
		fi; \
	done; \
	[ $$missing -eq 0 ] && echo "Runtime $(TARGET) defines all contract routines."

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
	@echo "Running diagnostics tests..."
	@bash tools/diag_test.sh
	@echo "Running run-time error tests..."
	@bash tools/runerr_test.sh
	@echo "Running the examples..."
	@bash examples/run.sh

# The test programs built for Windows and run there (on WSL through its
# interop, elsewhere with Wine registered for .exe files)
test-windows: $(EXEC)
	@echo "Running tests (x86_64-pc-windows-msvc)..."
	@passed=0; failed=0; \
	for cbl in tests/*.cbl; do \
		base=$$(basename $$cbl .cbl); \
		[ -f "tests/$$base.expected" ] || continue; \
		if TARGET=x86_64-pc-windows-msvc bash test_runner.sh $$cbl > /dev/null 2>&1; then \
			echo "  PASS  $$base"; passed=$$((passed+1)); \
		else \
			echo "  FAIL  $$base"; failed=$$((failed+1)); \
		fi; \
	done; \
	echo ""; echo "Results: $$passed passed, $$failed failed"; \
	[ $$failed -eq 0 ]

# Installing: the compiler, its documentation (PREFIX, DESTDIR as usual)
PREFIX ?= /usr/local
DESTDIR ?=
DOCDIR = $(DESTDIR)$(PREFIX)/share/doc/cobalt

install: $(EXEC)
	install -d $(DESTDIR)$(PREFIX)/bin $(DOCDIR)/guide
	install -m 755 $(EXEC) $(DESTDIR)$(PREFIX)/bin/cobalt
	install -m 644 README.md LICENSE $(DOCDIR)
	install -m 644 docs/guide/*.md $(DOCDIR)/guide

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/cobalt
	rm -f $(DOCDIR)/README.md $(DOCDIR)/LICENSE $(addprefix $(DOCDIR)/guide/,$(notdir $(wildcard docs/guide/*.md)))
	-rmdir $(DOCDIR)/guide $(DOCDIR)

# The example programs (examples/), built and run like the tests
examples: $(EXEC)
	@echo "Building the examples..."
	@bash examples/run.sh

.PHONY: all clean test test-windows runtime check-runtime windows install uninstall examples
