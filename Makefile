# Makefile for COBALT

CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude  # Debug build; add -O2 for release
LDFLAGS =  # Add libs later, e.g., -lm

SRCDIR = src
BINDIR = bin
OBJDIR = obj

# Find all .c files
SOURCES = $(shell find $(SRCDIR) -name '*.c')
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

EXEC = $(BINDIR)/cobalt

all: $(EXEC)

$(EXEC): $(OBJECTS) | $(BINDIR)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR):
	mkdir -p $(BINDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)
	@mkdir -p $(OBJDIR)/lexer $(OBJDIR)/parser $(OBJDIR)/backend $(OBJDIR)/ir $(OBJDIR)/sema

clean:
	rm -rf $(OBJDIR) $(BINDIR)

test:
	@echo "Running tests..."  # Add actual test commands later, e.g., run tests/*.cbl
	# For now, test with a sample: $(EXEC) -i tests/hello.cbl -o /dev/null

.PHONY: all clean test
