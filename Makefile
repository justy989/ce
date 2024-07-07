CC = clang
CFLAGS := -Wall -Werror -Wshadow -Wextra -Wno-unused-parameter -std=gnu11 -ggdb3
TERM_LDFLAGS := -rdynamic -lutil -ldl
GUI_LDFLAGS := -rdynamic -lSDL2 -lSDL2_ttf -lutil -ldl
TERM_DEFINES := -DDISPLAY_TERMINAL
GUI_DEFINES := -DDISPLAY_GUI

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	TERM_LDFLAGS += -lncursesw -pthread
	TERM_INCFLAGS += -I/usr/include/ncursesw
	GUI_LDFLAGS += -pthread
	GUI_INCFLAGS := -I/usr/include/SDL2
endif
ifeq ($(UNAME_S),Darwin)
	TERM_LDFLAGS += -lncurses -pthread
	GUI_LDFLAGS += -pthread
	GUI_INCFLAGS := -I/opt/homebrew/include/SDL2
endif
ifeq ($(findstring CYGWIN,$(UNAME_S)),CYGWIN)
	TERM_LDFLAGS += -lncursesw
	GUI_INCFLAGS := -I/usr/include/SDL2
endif

BUILD_DIR ?= build
TERM_OBJDIR ?= $(BUILD_DIR)/term
GUI_OBJDIR ?= $(BUILD_DIR)/gui

.PHONY: term gui clean install

TERM_EXE := ce_term
GUI_EXE := ce_gui

term: $(TERM_EXE)
gui: $(GUI_EXE)

TEST_CSRCS := $(wildcard test_*.c)
TESTS := $(patsubst %.c,%,$(TEST_CSRCS))

CSRCS := $(filter-out $(TEST_CSRCS), $(wildcard *.c))
# put our .o files in $(OBJDIR)
TERM_COBJS := $(patsubst %.c,$(TERM_OBJDIR)/%.o,$(CSRCS))
GUI_COBJS := $(patsubst %.c,$(GUI_OBJDIR)/%.o,$(CSRCS))
CHDRS := $(wildcard *.h)

$(TERM_OBJDIR):
	mkdir -p $@

$(TERM_OBJDIR)/%.o: %.c $(CHDRS) | $(TERM_OBJDIR)
	$(CC) $(TERM_DEFINES) $(CFLAGS) $(TERM_INCFLAGS) -c -o $@ $<

$(TERM_EXE): $(TERM_COBJS)
	$(CC) $(TERM_DEFINES) $(CFLAGS) $^ -o $@ $(TERM_LDFLAGS)

$(GUI_OBJDIR):
	mkdir -p $@

$(GUI_OBJDIR)/%.o: %.c $(CHDRS) | $(GUI_OBJDIR)
	$(CC) $(GUI_DEFINES) $(CFLAGS) -c -o $@ $<

$(GUI_EXE): $(GUI_COBJS)
	$(CC) $(GUI_DEFINES) $(CFLAGS) $^ -o $@ $(GUI_LDFLAGS)

test: $(TESTS)

test_ce: test_ce.c $(TERM_OBJDIR)/ce.o $(TERM_OBJDIR)/ce_regex_linux.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	./$@

test_ce_json: test_ce_json.c $(TERM_OBJDIR)/ce_json.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	./$@

clean:
	rm -f $(TERM_EXE) $(GUI_EXE) $(TESTS) ce_test.log valgrind.out
	rm -rf $(TERM_OBJDIR) $(GUI_OBJDIR)
