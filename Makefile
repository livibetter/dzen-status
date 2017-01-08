PROGRAM = dzen-status

CC       ?= gcc
CFLAGS   ?= -g -O2 -Wall -Wextra -Werror
CFLAGS   += -std=c99
CPPFLAGS += -D_POSIX_C_SOURCE=199309L
LDLIBS   += -lasound

.PHONY: all
all: $(PROGRAM)

$(PROGRAM): dzen-status.c

.PHONY: clean
clean:
	$(RM) $(PROGRAM)
