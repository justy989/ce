CC ?= clang
CFLAGS := -Wall -Werror -Wshadow -Wextra -Wno-unused-parameter -std=gnu11 -ggdb3
LDFLAGS := -rdynamic -pthread -lncursesw -lutil -ldl

OBJDIR ?= build

.PHONY: all clean

all: ce

CSRCS := $(shell ls *.c | grep -v test_*)
# put our .o files in $(OBJDIR)
COBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(CSRCS))
CHDRS := $(wildcard *.h)

$(OBJDIR):
	mkdir $@

$(OBJDIR)/%.o: %.c $(CHDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c  -o $@ $<

ce: $(COBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: test_ce
test_ce: test_ce.c ce.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	./test_ce

clean:
	rm -f ce test_ce ce_test.log valgrind.out
	rm -rf $(OBJDIR)
