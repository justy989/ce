# ce: a c language terminal editor

[![Build Status](https://travis-ci.org/justy989/ce.svg?branch=master)](https://travis-ci.org/justy989/ce)

### But why?
- Emacs and Vim are awesome, but they have some problems we'd like to address without having to learn/write
  vimscript or emacs lisp. However, we want to take some awesome ideas from each:
  - Emacs's idea where everything is a just a plain text buffer. We can do things like, run shell
    commands that output to a buffer just like any other. So you can copy+paste, etc.
  - Vim's modal editting. We really like it! It also makes the transition to using ce easier.
    (Note: the default configuration implements vim-like editting, but your configuration can implement
    any arbitrary editting style)
- The config is written in the C programming language and compiled into a shared object that you can reload
  while running. No need to learn a new language!
- Registers in vim are awesome, but it's not easy to see what you have in registers or even know what
  registers are occupied. In ce, for example, you can type 'q?' to see which macros you have defined. This
  also works for yank registers and mark registers.
- Search in vim/emacs with regular expressions is awesome, but each have a special implementation of regexes
  with special syntax in certain cases. We just want to use the c standard library's regex implementation, so we
  don't have to remember special rules. We also support plain search.
- Macros in vim are awesome, but:
  - If you mess up while creating a macro, you have to start over. In ce, when you are viewing your recorded macros,
    you can select any macro to edit it
- Syntax Highlighting in emacs and vim are done and extended using regexes, which are not great to read. In ce,
  a c interface is provided for custom syntax highlighting. See ce_syntax.h for examples.
- The authors need to work remotely, so the editor needs to be able to run in a terminal.
- It is fun learning how to make a text editor.
- Using something on a daily basis that we created gives us the warm and fuzzies.

### How To Build
- Requirements
  - c11 compiler
  - ncurses library
- Step(s)
  - `$ make`

### How To Run
`$ ce path/to/file.c`

### Default Keybindings (in normal or visual mode)
Key Sequence|Action
------------|------
`Ctrl+f`|load file
`Ctrl+q`|kill current window or stop recording macro
`Ctrl+w`|save buffer
`Ctrl+t`|create new tab
`Ctrl+x`|goto last terminal
`Ctrl+u`|page up
`Ctrl+d`|page down
`Ctrl+b`|switch buffer dialogue
`Ctrl+r`|redo edit
`Ctrl+a`|select parent view
`Ctrl+v`|vertical split
`Ctrl+s`|horizontal split
`Ctrl+h`|move cursor to the view to the left
`Ctrl+j`|move cursor to the view to the below
`Ctrl+k`|move cursor to the view to the above
`Ctrl+l`|move cursor to the view to the right
`Ctrl+n`|goto the next file definition in the shell command buffer (works with compilation errors, fgrep, git diff, etc)
`Ctrl+p`|goto the previous file definition in the shell command buffer (works with compilation errors, fgrep, git diff, etc)
`Ctrl+o`|goto previous jump list location
`Ctrl+i`|goto next jump list location
`return`|in input mode, then confirm input action, in normal mode, set mark in 0 register, 
`space`|in normal mode, goto mark in 0 register
`i`|enter insert mode
`esc`|enter normal mode
`h`|move cursor left
`j`|move cursor down
`k`|move cursor up
`l`|move cursor right`|
`w`|move by word
`e`|move to end of word
`b`|move to beginning of word
`^`|move to soft beginning of line (on an empty line insert whitespace to get to indentation level)
`0`|move to hard beginning of line
`$`|move to hard end of line
`c`|change action
`d`|delete action
`r`|replace character
`x`|remove character
`s`|remove character and enter insert
`f`|goto character on line (next character typed)
`t`|goto before character on line (next character typed)
`y`|yank
`p`|paste after cursor
`P`|paste before cursor
`~`|flip alphabetical character's case
`u`|undo edit
`/`|incremental search forward
`?`|incremental search backward
`n`|goto next search match
`N`|goto previous search match
`v`|visual mode
`V`|visual line mode
`o`|insert a new line and move the cursor
`O`|insert a new line before the cursor and move the cursor
`m`|set mark in register (next character typed)
`m?`|view mark registers (confirm on register you want to goto)
`q`|record macro to a register (next character typed)
`@`|replay macro from a register (next character typed)
`@?`|view macro registers (confirm on register you want to edit)
`q?`|view macro registers (confirm on register you want to edit)
`"`|specify yank or paste from a specific register (next character typesd)
`"?`|view yank registers (confirm on register you want to edit)
`y?`|view yank registers (confirm on register you want to edit)
`zt`|scroll view so cursor is at the top
`zz`|scroll view so cursor is in the middle
`zb`|scroll view so cursor is at the bottom
`\q`|quit editor
`gt`|goto next tab
`gT`|goto previous tab
`<<`|unindent current line or all lines in visual selection
`>>`|indent current line or all lines in visual selection
`%`|find matching quotes, parents, brackets, square brackets, angled brackets
`\*`|search forward for the word under the cursor
`#`|search forward for the word under the cursor
`:`|in normal mode, run command
`F5`|reload ce's config

### Commands (press `:` in normal mode)
Name|Action
----|------
reload_buffer|reload the file associated with the current buffer
new_buffer|open a new empty and unnamed buffer
rename|rename current buffer
noh|turn off highlighting search matches(until you search again)
syntax|set syntax mode
line_number|set line number mode
quit|quit ce
select_adjacent_layout|select 'left', 'right', 'up' or 'down adjacent layouts
save_buffer|save the currently selected view's buffer
show_buffers|show the list of buffers
show_yanks|show the state of your vim yanks
split_layout|split the current layout 'horizontal' or 'vertical' into 2 layouts
select_parent_layout|select the parent of the current layout
delete_layout|delete the current layout (unless it's the only one left)
load_file|load a file (optionally specified)
new_tab|create a new tab
select_adjacent_tab|selects either the 'left' or 'right' tab
search|interactive search 'forward' or 'backward'
regex_search|interactive regex search 'forward' or 'backward'
noh|turn off search highlighting
command|interactively send a commmand
redraw|redraw the entire editor
switch_to_terminal|if the terminal is in view, goto it, otherwise, open the terminal in the current view
switch_buffer|open dialogue to switch buffer by name
goto_destination_in_line|scan current line for destination formats
goto_next_destination|find the next line in the buffer that contains a destination to goto
goto_prev_destination|find the previous line in the buffer that contains a destination to goto
replace_all|replace all occurances below cursor (or within a visual range)
reload_file|reload the file in the current view, overwriting any changes outstanding
reload_config|reload the config shared object
buffer_type|set the current buffer's type: c, python, java, bash, config, diff, plain
new_buffer|create a new buffer
rename_buffer|rename the current buffer
jump_list|jump to 'next' or 'previous' jump location based on argument passed in
line_number|change line number mode: 'none', 'absolute', 'relative', or 'both'
terminal_command|run a command in the terminal
terminal_command_in_view|run a command in the terminal, and switch to it in view
man_page_on_word_under_cursor|run man on the word under the cursor

### Cool Shell Commands To Run In ce
`$ fgrep -n -H <pattern> <files>`  
`$ fgrep -n -H Buffer ce_config.c`  
`$ fgrep -n default *`  
ctrl+n and ctrl+p will move you between matches  
  
`$ make`  
ctrl+n and ctrl+p will move you between build failures  
  
`$ cscope -L1<symbol>`  
`$ cscope -L1BufferView`  
ctrl+n and ctrl+p will move you between definitions of that symbol (if tags have been generated)  
  
`$ cscope -L3ce_insert_char`  
ctrl+n and ctrl+p will move you between functions calling this function (if tags have been generated)  
