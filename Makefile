CC = clang
CFLAGS := -Wall -Werror -Wshadow -Wextra -Wno-unused-parameter -std=gnu11 -ggdb3
LDFLAGS := -rdynamic -pthread -lncursesw -lutil -ldl
TERM_DEFINES := -DDISPLAY_TERMINAL
# LDFLAGS := -rdynamic -lncursesw -lutil -ldl

BUILD_DIR ?= build
TERM_OBJDIR ?= $(BUILD_DIR)/term

.PHONY: term clean install

TERM_EXE := ce_term

term: $(TERM_EXE)

TEST_CSRCS := $(wildcard test_*.c)
TESTS := $(patsubst %.c,%,$(TEST_CSRCS))

CSRCS := $(filter-out $(TEST_CSRCS), $(wildcard *.c))
# put our .o files in $(OBJDIR)
TERM_COBJS := $(patsubst %.c,$(TERM_OBJDIR)/%.o,$(CSRCS))
CHDRS := $(wildcard *.h)

$(TERM_OBJDIR):
	mkdir -p $@

$(TERM_OBJDIR)/%.o: %.c $(CHDRS) | $(TERM_OBJDIR)
	$(CC) $(TERM_DEFINES) $(CFLAGS) -c -o $@ $<

$(TERM_EXE): $(TERM_COBJS)
	$(CC) $(TERM_DEFINES) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TESTS)

test_%: test_%.c $(TERM_OBJDIR)/%.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	./$@

clean:
	rm -f $(TERM_EXE) $(TESTS) ce_test.log valgrind.out
	rm -rf $(BUILD_DIR)
