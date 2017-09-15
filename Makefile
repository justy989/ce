CC ?= clang
CFLAGS := -Wall -Werror -Wshadow -Wextra -Wno-unused-parameter -std=gnu11 -ggdb3
LDFLAGS := -rdynamic -pthread -lncursesw -lutil -ldl

builddir ?= build

.PHONY: all clean

all: $(builddir) ce

CSRCS := $(wildcard *.c)
# put our .o files in $(builddir)
COBJS := $(patsubst %.c,$(builddir)/%.o,$(CSRCS))
CHDRS := $(wildcard *.h)

$(builddir):
	mkdir $@

$(builddir)/%.o: %.c $(CHDRS)
	$(CC) $(CFLAGS) -c  -o $@ $<

ce: $(COBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f ce test_ce ce_test.log valgrind.out
	rm -r $(builddir)
