# ce: a c language terminal editor

### But why?
- Emacs and Vim are awesome, but they have some problems we'd like to address without having to learn/write
  vimscript or emacs lisp. However, we want to take some awesome ideas from each:
  - Emacs's idea where everything is a just a plain text buffer. We get a familiar and common interface for
    all buffer types and can copy+paste from anywhere (even the terminal)
  - Vim's modal editting. We really like it! It also makes the transition to using ce easier.
    (Note: the default configuration implements vim-like editting)
- The config is written in the C programming language and compiled into a shared object that you can reload
  while running. No need to learn a new language!
- Registers in vim are awesome, but it's not easy to see what you have in registers or even know what
  registers are occupied. In ce, you have info buffers '[marks]', '[macros]', '[yanks]' to show what you have
  in each register.
- Search in vim/emacs with regular expressions is awesome, but each have a special implementation of regexes
  with special syntax in certain cases. We just want to use the c standard library's regex implementation, so we
  don't have to remember special rules. We also support plain search.
- Macros in vim are awesome, but:
  - If you mess up while creating a macro, you have to start over. In ce, when you are viewing your recorded macros,
    you can select any macro to edit it
- Syntax Highlighting in emacs and vim are done and extended using regexes, which are not great to read. In ce,
  a c interface is provided for custom syntax highlighting. See ce_syntax.h for examples.
- When using the terminal, there is often output with file and line numbers (compiler errors, greps, etc) that we
  would like to easily scroll through. Ctrl+n and Ctrl+p scans the last terminal for lines with common file:line
  patterns and jumps you to the file relative to the terminal directory.
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

### How to configure
Build a shared object that implements these functions:
``` c
bool ce_init(CeApp_t* app){
     // initialize your config here
     return true;
}

bool ce_free(CeApp_t* app){
     // clean up your config here
     return true;
}
```

When running ce, pass it the path to the shared object
`$ ce -c path/to/config.so`

See https://www.github.com/justy989/ce_config for an example configuration


### Default Keybindings (in normal or visual mode)
Key Sequence|Action
------------|------
`:`|in normal mode, run command
`Ctrl+f`|load file
`Ctrl+s`|save buffer
`Ctrl+u`|page up
`Ctrl+d`|page down
`Ctrl+b`|switch buffer dialogue
`Ctrl+r`|redo edit
`Ctrl+w h`|move cursor to the view to the left
`Ctrl+w j`|move cursor to the view to the below
`Ctrl+w k`|move cursor to the view to the above
`Ctrl+w l`|move cursor to the view to the right
`Ctrl+o`|goto previous jump list location
`Ctrl+i`|goto next jump list location
`Ctrl+n`|goto the next file definition in the shell command buffer (works with compilation errors, fgrep, etc)
`Ctrl+p`|goto the previous file definition in the shell command buffer (works with compilation errors, fgrep, etc)
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
`\/`|incremental regex search forward
`\?`|incremental regex search backward
`n`|goto next search match
`N`|goto previous search match
`v`|visual mode
`V`|visual line mode
`gV`|visual block mode
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
`\b`|show buffer list in view
`gt`|goto next tab
`gT`|goto previous tab
`<<`|unindent current line or all lines in visual selection
`>>`|indent current line or all lines in visual selection
`%`|find matching quotes, parents, brackets, square brackets, angled brackets
`\*`|search forward for the word under the cursor
`#`|search forward for the word under the cursor

### Commands (press `:` in normal mode)
Name|Action
----|------
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
setpaste|about to paste, so turn off auto indentation
setnopaste|done pasting, so turn on auto indentation again
command|interactively send a commmand
redraw|redraw the entire editor
switch_to_terminal|if the terminal is in view, goto it, otherwise, open the terminal in the current view
new_terminal|open a new terminal and show it in the current view
switch_buffer|open dialogue to switch buffer by name
goto_destination_in_line|scan current line for destination formats
goto_next_destination|find the next line in the buffer that contains a destination to goto
goto_prev_destination|find the previous line in the buffer that contains a destination to goto
replace_all|replace all occurances below cursor (or within a visual range) with the previous search
reload_file|reload the file in the current view, overwriting any changes outstanding
reload_config|reload the config shared object
buffer_type|set the current buffer's type: c, cpp, python, java, bash, config, diff, plain
new_buffer|create a new buffer
rename_buffer|rename the current buffer
jump_list|jump to 'next' or 'previous' jump location based on argument passed in
line_number|change line number mode: 'none', 'absolute', 'relative', or 'both'
terminal_command|run a command in the terminal
terminal_command_in_view|run a command in the terminal, and switch to it in view
man_page_on_word_under_cursor|run man on the word under the cursor
