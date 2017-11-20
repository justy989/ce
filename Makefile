CC = clang
CFLAGS := -Wall -Werror -Wshadow -Wextra -Wno-unused-parameter -std=gnu11 -ggdb3
LDFLAGS := -rdynamic -pthread -lncursesw -lutil -ldl

OBJDIR ?= build

.PHONY: all clean

all: ce

TEST_CSRCS := $(wildcard test_*.c)
TESTS := $(patsubst %.c,%,$(TEST_CSRCS))

CSRCS := $(filter-out $(TEST_CSRCS), $(wildcard *.c))
# put our .o files in $(OBJDIR)
COBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(CSRCS))
CHDRS := $(wildcard *.h)

$(OBJDIR):
	mkdir $@

$(OBJDIR)/%.o: %.c $(CHDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c  -o $@ $<

ce: $(COBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TESTS)

test_%: test_%.c $(OBJDIR)/%.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	./$@

clean:
	rm -f ce $(TESTS) ce_test.log valgrind.out
	rm -rf $(OBJDIR)
