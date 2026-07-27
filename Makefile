# Makefile for COBALT

CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude  # Debug build; add -O2 for release
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

all: $(EXEC)

$(EXEC): $(OBJECTS) | $(BINDIR)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Include auto-generated dependency files (silently ignore if missing)
-include $(DEPS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)
	@mkdir -p $(OBJDIR)/lexer $(OBJDIR)/parser $(OBJDIR)/backend $(OBJDIR)/ir $(OBJDIR)/sema

clean:
	rm -rf $(OBJDIR) $(BINDIR)

test: $(EXEC)
	@echo "Running tests..."
	@passed=0; failed=0; \
	for cbl in tests/*.cbl; do \
		base=$$(basename $$cbl .cbl); \
		if [ -f "tests/$$base.expected" ]; then \
			if bash test_runner.sh $$cbl > /dev/null 2>&1; then \
				echo "  PASS  $$base"; passed=$$((passed+1)); \
			else \
				echo "  FAIL  $$base"; failed=$$((failed+1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "Results: $$passed passed, $$failed failed"; \
	[ $$failed -eq 0 ]

.PHONY: all clean test
