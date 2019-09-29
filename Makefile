PROGRAM = dzen-status

CC       ?= gcc
CFLAGS   ?= -g -O2 -Wall -Wextra -Werror
CFLAGS   += -std=c99
CPPFLAGS += -D_POSIX_C_SOURCE=199309L
LDLIBS   += -lasound

INSTALL     = install
INSTALL_BIN = $(INSTALL) -D -m 755

PREFIX = /usr/local

BIN_DIR = $(PREFIX)/bin

.PHONY: all
all: $(PROGRAM)

$(PROGRAM): dzen-status.c

.PHONY: clean
clean:
	$(RM) $(PROGRAM)

install:
	$(INSTALL_BIN) $(PROGRAM) $(DESTDIR)$(BIN_DIR)/$(PROGRAM)
.PHONY: install

uninstall:
	$(RM) $(DESTDIR)$(BIN_DIR)/$(PROGRAM)
.PHONY: uninstall
