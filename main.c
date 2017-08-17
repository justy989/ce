#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <ncurses.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>

#include "ce.h"
#include "ce_vim.h"
#include "ce_layout.h"
#include "ce_command.h"
#include "ce_syntax.h"
#include "ce_terminal.h"
#include "ce_complete.h"
#include "ce_macros.h"

#define UNSAVED_BUFFERS_DIALOGUE "UNSAVED BUFFERS, QUIT? [Y/N]"

char* directory_from_filename(const char* filename){
     const char* last_slash = strrchr(filename, '/');
     char* directory = NULL;
     if(last_slash) directory = strndup(filename, last_slash - filename);
     return directory;
}

typedef struct BufferNode_t{
     CeBuffer_t* buffer;
     struct BufferNode_t* next;
}BufferNode_t;

bool buffer_node_insert(BufferNode_t** head, CeBuffer_t* buffer){
     BufferNode_t* node = malloc(sizeof(*node));
     if(!node) return false;
     node->buffer = buffer;
     node->next = *head;
     *head = node;
     return true;
}

void buffer_node_free(BufferNode_t** head){
     BufferNode_t* itr = *head;
     while(itr){
          BufferNode_t* tmp = itr;
          itr = itr->next;
          ce_buffer_free(tmp->buffer);
          free(tmp->buffer);
          free(tmp);
     }
     *head = NULL;
}

bool buffer_append_on_new_line(CeBuffer_t* buffer, const char* string){
     int64_t last_line = buffer->line_count;
     if(last_line) last_line--;
     int64_t line_len = ce_utf8_strlen(buffer->lines[last_line]);
     if(line_len){
          if(!ce_buffer_insert_string(buffer, "\n", (CePoint_t){line_len, last_line})) return false;
     }
     int64_t next_line = last_line;
     if(line_len) next_line++;
     return ce_buffer_insert_string(buffer, string, (CePoint_t){0, next_line});
}

static void build_buffer_list(CeBuffer_t* buffer, BufferNode_t* head){
     int64_t index = 1;
     char line[256];
     ce_buffer_empty(buffer);
     while(head){
          snprintf(line, 256, "%ld %s %ld", index, head->buffer->name, head->buffer->line_count);
          buffer_append_on_new_line(buffer, line);
          head = head->next;
          index++;
     }

     buffer->status = CE_BUFFER_STATUS_READONLY;
}

static void build_yank_list(CeBuffer_t* buffer, CeVimYank_t* yanks){
     char line[256];
     ce_buffer_empty(buffer);
     for(int64_t i = 0; i < CE_ASCII_PRINTABLE_CHARACTERS; i++){
          CeVimYank_t* yank = yanks + i;
          if(yank->text == NULL) continue;
          char reg = i + '!';
          snprintf(line, 256, "// register '%c': line: %s\n%s\n", reg, yank->line ? "true" : "false", yank->text);
          buffer_append_on_new_line(buffer, line);
     }

     buffer->status = CE_BUFFER_STATUS_READONLY;
}

static void build_complete_list(CeBuffer_t* buffer, CeComplete_t* complete){
     ce_buffer_empty(buffer);
     char line[256];
     int64_t cursor = 0;
     for(int64_t i = 0; i < complete->count; i++){
          if(complete->elements[i].match){
               if(i == complete->current){
                    cursor = buffer->line_count;
                    snprintf(line, 256, "*%s", complete->elements[i].string);
               }else{
                    snprintf(line, 256, " %s", complete->elements[i].string);
               }
               buffer_append_on_new_line(buffer, line);
          }
     }

     // TODO: figure out why we have to account for this case
     if(buffer->line_count == 1 && cursor == 1) cursor = 0;

     buffer->cursor_save = (CePoint_t){0, cursor};
     buffer->status = CE_BUFFER_STATUS_READONLY;
}

static void build_macro_list(CeBuffer_t* buffer, CeMacros_t* macros){
     ce_buffer_empty(buffer);
     char line[256];
     for(int64_t i = 0; i < CE_ASCII_PRINTABLE_CHARACTERS; i++){
          CeRuneNode_t* rune_head = macros->rune_head[i];
          if(!rune_head) continue;
          CeRune_t* rune_string = ce_rune_node_string(rune_head);
          char* string = ce_rune_string_to_char_string(rune_string);
          char reg = i + '!';
          snprintf(line, 256, "// register '%c'\n%s", reg, string);
          free(rune_string);
          free(string);
          buffer_append_on_new_line(buffer, line);
     }

     // NOTE: must keep this in sync with ce_rune_string_to_char_string()
     buffer_append_on_new_line(buffer, "");
     buffer_append_on_new_line(buffer, "// escape conversions");
     buffer_append_on_new_line(buffer, "// \\b -> KEY_BACKSPACE");
     buffer_append_on_new_line(buffer, "// \\e -> KEY_ESCAPE");
     buffer_append_on_new_line(buffer, "// \\r -> KEY_ENTER");
     buffer_append_on_new_line(buffer, "// \\t -> KEY_TAB");
     buffer_append_on_new_line(buffer, "// \\u -> KEY_UP");
     buffer_append_on_new_line(buffer, "// \\d -> KEY_DOWN");
     buffer_append_on_new_line(buffer, "// \\l -> KEY_LEFT");
     buffer_append_on_new_line(buffer, "// \\i -> KEY_RIGHT");
     buffer_append_on_new_line(buffer, "// \\\\ -> \\"); // HAHAHAHAHA

     buffer->status = CE_BUFFER_STATUS_READONLY;
}

void view_switch_buffer(CeView_t* view, CeBuffer_t* buffer, CeVim_t* vim, CeConfigOptions_t* config_options){
     // save the cursor on the old buffer
     view->buffer->cursor_save = view->cursor;
     view->buffer->scroll_save = view->scroll;

     // update new buffer, using the buffer's cursor
     view->buffer = buffer;
     view->cursor = buffer->cursor_save;
     view->scroll = buffer->scroll_save;

     ce_view_follow_cursor(view, config_options->horizontal_scroll_off, config_options->vertical_scroll_off, config_options->tab_width);

     vim->mode = CE_VIM_MODE_NORMAL;
}

static CeBuffer_t* load_file_into_view(BufferNode_t** buffer_node_head, CeView_t* view,
                                       CeConfigOptions_t* config_options, CeVim_t* vim, const char* filepath){
     // have we already loaded this file?
     BufferNode_t* itr = *buffer_node_head;
     while(itr){
          if(strcmp(itr->buffer->name, filepath) == 0){
               view_switch_buffer(view, itr->buffer, vim, config_options);
               return itr->buffer;
          }
          itr = itr->next;
     }

     // load file
     CeBuffer_t* buffer = calloc(1, sizeof(*buffer));
     if(ce_buffer_load_file(buffer, filepath)){
          buffer_node_insert(buffer_node_head, buffer);
          view_switch_buffer(view, buffer, vim, config_options);

          // TODO: figure out type based on extention
          buffer->type = CE_BUFFER_FILE_TYPE_C;
     }else{
          free(buffer);
          return NULL;
     }

     return buffer;
}

// limit to 60 fps
#define DRAW_USEC_LIMIT 16666

bool custom_vim_verb_substitute(CeVim_t* vim, const CeVimAction_t* action, CeVimMotionRange_t motion_range, CeView_t* view, \
                                const CeConfigOptions_t* config_options){
     char reg = action->verb.character;
     if(reg == 0) reg = '"';
     CeVimYank_t* yank = vim->yanks + ce_vim_yank_register_index(reg);
     if(!yank->text) return false;

     bool do_not_include_end = ce_vim_motion_range_sort(&motion_range);

     if(action->motion.function == ce_vim_motion_little_word ||
        action->motion.function == ce_vim_motion_big_word ||
        action->motion.function == ce_vim_motion_begin_little_word ||
        action->motion.function == ce_vim_motion_begin_big_word){
          do_not_include_end = true;
     }

     // delete the range
     if(do_not_include_end) motion_range.end = ce_buffer_advance_point(view->buffer, motion_range.end, -1);
     int64_t delete_len = ce_buffer_range_len(view->buffer, motion_range.start, motion_range.end);
     char* removed_string = ce_buffer_dupe_string(view->buffer, motion_range.start, delete_len);
     if(!ce_buffer_remove_string(view->buffer, motion_range.start, delete_len)){
          free(removed_string);
          return false;
     }

     // commit the change
     CeBufferChange_t change = {};
     change.chain = false;
     change.insertion = false;
     change.string = removed_string;
     change.location = motion_range.start;
     change.cursor_before = view->cursor;
     change.cursor_after = motion_range.start;
     ce_buffer_change(view->buffer, &change);

     // insert the yank
     int64_t yank_len = ce_utf8_strlen(yank->text);
     if(!ce_buffer_insert_string(view->buffer, yank->text, motion_range.start)) return false;
     CePoint_t cursor_end = ce_buffer_advance_point(view->buffer, motion_range.start, yank_len);

     // commit the change
     change.chain = true;
     change.insertion = true;
     change.string = strdup(yank->text);
     change.location = motion_range.start;
     change.cursor_before = view->cursor;
     change.cursor_after = cursor_end;
     ce_buffer_change(view->buffer, &change);

     view->cursor = cursor_end;
     vim->chain_undo = action->chain_undo;

     return true;
}

CeVimParseResult_t custom_vim_parse_verb_substitute(CeVimAction_t* action, CeRune_t key){
     action->verb.function = &custom_vim_verb_substitute;
     action->repeatable = true;
     return CE_VIM_PARSE_IN_PROGRESS;
}

void syntax_highlight_terminal(CeView_t* view, volatile CeTerminal_t* terminal, CeDrawColorList_t* draw_color_list,
                               CeSyntaxDef_t* syntax_defs){
     if(!view->buffer) return;
     if(view->buffer->line_count <= 0) return;
     int64_t min = view->scroll.y;
     int64_t max = min + (view->rect.bottom - view->rect.top);
     int64_t clamp_max = (view->buffer->line_count - 1);
     if(clamp_max < 0) clamp_max = 0;
     CE_CLAMP(min, 0, clamp_max);
     CE_CLAMP(max, 0, clamp_max);
     int fg = COLOR_DEFAULT;
     int bg = COLOR_DEFAULT;

     for(int64_t y = min; y <= max; ++y){
          for(int64_t x = 0; x < terminal->columns; ++x){
               CeTerminalGlyph_t* glyph = terminal->lines[y] + x;
               if(glyph->foreground != fg || glyph->background != bg){
                    fg = glyph->foreground;
                    bg = glyph->background;
                    ce_draw_color_list_insert(draw_color_list, fg, bg, (CePoint_t){x, y});
               }
          }
     }
}

typedef struct{
     int* keys;
     int64_t key_count;
     CeCommand_t command;
     CeVimMode_t vim_mode;
}KeyBind_t;

typedef struct{
     KeyBind_t* binds;
     int64_t count;
}KeyBinds_t;

typedef struct{
     int keys[4];
     const char* command;
}KeyBindDef_t;

static void convert_bind_defs(KeyBinds_t* binds, KeyBindDef_t* bind_defs, int64_t bind_def_count)
{
     binds->count = bind_def_count;
     binds->binds = malloc(binds->count * sizeof(*binds->binds));

     for(int64_t i = 0; i < binds->count; ++i){
          ce_command_parse(&binds->binds[i].command, bind_defs[i].command);
          binds->binds[i].key_count = 0;

          for(int k = 0; k < 4; ++k){
               if(bind_defs[i].keys[k] == 0) break;
               binds->binds[i].key_count++;
          }

          if(!binds->binds[i].key_count) continue;

          binds->binds[i].keys = malloc(binds->binds[i].key_count * sizeof(binds->binds[i].keys[0]));

          for(int k = 0; k < binds->binds[i].key_count; ++k){
               binds->binds[i].keys[k] = bind_defs[i].keys[k];
          }
     }
}

#define APP_MAX_KEY_COUNT 16

typedef struct{
     CeRect_t terminal_rect;
     CeVim_t vim;
     CeConfigOptions_t config_options;
     int terminal_width;
     int terminal_height;
     CeView_t input_view;
     CeView_t complete_view;
     bool input_mode;
     CeLayout_t* tab_list_layout;
     CeSyntaxDef_t* syntax_defs;
     BufferNode_t* buffer_node_head;
     CeCommandEntry_t* command_entries;
     int64_t command_entry_count;
     CeVimParseResult_t last_vim_handle_result;
     CeBuffer_t* buffer_list_buffer;
     CeBuffer_t* yank_list_buffer;
     CeBuffer_t* complete_list_buffer;
     CeBuffer_t* macro_list_buffer;
     CeComplete_t command_complete;
     CeComplete_t load_file_complete;
     CeComplete_t switch_buffer_complete;
     KeyBinds_t key_binds[CE_VIM_MODE_COUNT];
     CeRune_t keys[APP_MAX_KEY_COUNT];
     int64_t key_count;
     char edit_register;
     CeTerminal_t terminal;
     CeMacros_t macros;
     CePoint_t search_start;
     bool record_macro;
     bool replay_macro;
     bool ready_to_draw;
     bool quit;
}App_t;

void app_update_terminal_view(App_t* app){
     getmaxyx(stdscr, app->terminal_height, app->terminal_width);
     app->terminal_rect = (CeRect_t){0, app->terminal_width - 1, 0, app->terminal_height - 1};
     ce_layout_distribute_rect(app->tab_list_layout, app->terminal_rect, app->config_options.horizontal_scroll_off,
                               app->config_options.vertical_scroll_off, app->config_options.tab_width);
}

CeComplete_t* app_is_completing(App_t* app){
     if(app->input_mode){
          if(strcmp(app->input_view.buffer->name, "COMMAND") == 0) return &app->command_complete;
          if(strcmp(app->input_view.buffer->name, "LOAD FILE") == 0) return &app->load_file_complete;
          if(strcmp(app->input_view.buffer->name, "SWITCH BUFFER") == 0) return &app->switch_buffer_complete;
     }

     return NULL;
}

void complete_files(CeComplete_t* complete, const char* line, const char* base_directory){
     char full_path[PATH_MAX];
     if(base_directory){
          snprintf(full_path, PATH_MAX, "%s/%s", base_directory, line);
     }else{
          strncpy(full_path, line, PATH_MAX);
     }

     // figure out the directory to complete
     const char* last_slash = strrchr(full_path, '/');
     char* directory = NULL;

     if(last_slash){
          directory = strndup(full_path, (last_slash - full_path) + 1);
     }else{
          directory = strdup(".");
     }

     // build list of files to complete
     struct dirent *node;
     DIR* os_dir = opendir(directory);
     if(!os_dir) return; // TODO: leak directory

     int64_t file_count = 0;
     char** files = malloc(sizeof(*files));

     char tmp[PATH_MAX];
     struct stat info;
     while((node = readdir(os_dir)) != NULL){
          snprintf(tmp, PATH_MAX, "%s/%s", directory, node->d_name);
          stat(tmp, &info);
          file_count++;
          files = realloc(files, file_count * sizeof(*files));
          if(S_ISDIR(info.st_mode)){
               asprintf(&files[file_count - 1], "%s/", node->d_name);
          }else{
               files[file_count - 1] = strdup(node->d_name);
          }
     }

     closedir(os_dir);

     // check for exact match
     bool exact_match = true;
     if(complete->count != file_count){
          exact_match = false;
     }else{
          for(int64_t i = 0; i < file_count; i++){
               if(strcmp(files[i], complete->elements[i].string) != 0){
                    exact_match = false;
                    break;
               }
          }
     }

     if(!exact_match){
          ce_complete_init(complete, (const char**)(files), file_count);
     }

     for(int64_t i = 0; i < file_count; i++){
          free(files[i]);
     }
     free(files);

     if(last_slash){
          ce_complete_match(complete, last_slash + 1);
     }else{
          ce_complete_match(complete, line);
     }
}

static char* view_base_directory(CeView_t* view, App_t* app){
     if(view->buffer == app->terminal.buffer){
          return ce_terminal_get_current_directory(&app->terminal);
     }

     return directory_from_filename(view->buffer->name);
}

void draw_view(CeView_t* view, int64_t tab_width, CeDrawColorList_t* draw_color_list, CeColorDefs_t* color_defs){
     int64_t view_height = view->rect.bottom - view->rect.top;
     int64_t view_width = view->rect.right - view->rect.left;
     int64_t row_min = view->scroll.y;
     int64_t col_min = view->scroll.x;
     int64_t col_max = col_min + view_width;

     char tab_str[tab_width + 1];
     memset(tab_str, ' ', tab_width);
     tab_str[tab_width] = 0;

     CeDrawColorNode_t* draw_color_node = draw_color_list->head;

     standend();
     if(view->buffer->line_count > 0){
          move(0, 0);

          for(int64_t y = 0; y < view_height; y++){
               int64_t index = 0;
               int64_t x = 0;
               int64_t rune_len = 0;
               int64_t line_index = y + row_min;
               CeRune_t rune = 1;

               move(view->rect.top + y, view->rect.left);

               if(line_index < view->buffer->line_count){
                    const char* line = view->buffer->lines[y + row_min];

                    while(rune > 0){
                         rune = ce_utf8_decode(line, &rune_len);

                         // check if we need to move to the next color
                         while(draw_color_node && !ce_point_after(draw_color_node->point, (CePoint_t){index, y + view->scroll.y})){
                              int change_color_pair = ce_color_def_get(color_defs, draw_color_node->fg, draw_color_node->bg);
                              attron(COLOR_PAIR(change_color_pair));
                              draw_color_node = draw_color_node->next;
                         }

                         if(x >= col_min && x <= col_max && rune > 0){
                              if(rune == CE_TAB){
                                   x += tab_width;
                                   addstr(tab_str);
                              }else if(rune >= 0x80){
                                   char utf8_string[CE_UTF8_SIZE + 1];
                                   int64_t bytes_written = 0;
                                   ce_utf8_encode(rune, utf8_string, CE_UTF8_SIZE, &bytes_written);
                                   utf8_string[bytes_written] = 0;
                                   addstr(utf8_string);
                                   x++;
                              }else{
                                   addch(rune);
                                   x++;
                              }
                         }else if(rune == CE_TAB){
                              x += tab_width;
                         }else{
                              x++;
                         }

                         line += rune_len;
                         index++;
                    }
               }

               if(x < col_min) x = col_min + 1;

               standend();
               for(; x <= col_max + 1; x++) addch(' ');
          }
     }
}

void draw_view_status(CeView_t* view, CeVim_t* vim, CeMacros_t* macros, CeColorDefs_t* color_defs, int64_t height_offset){
     // create bottom bar bg
     int64_t bottom = view->rect.bottom + height_offset;
     int color_pair = ce_color_def_get(color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
     attron(COLOR_PAIR(color_pair));
     int64_t width = (view->rect.right - view->rect.left) + 1;
     move(bottom, view->rect.left);
     for(int64_t i = 0; i < width; ++i){
          addch(' ');
     }

     // set the mode line
     int vim_mode_fg = COLOR_DEFAULT;
     const char* vim_mode_string = NULL;

     if(vim){
          switch(vim->mode){
          default:
               break;
          case CE_VIM_MODE_NORMAL:
               vim_mode_string = "N";
               vim_mode_fg = COLOR_BLUE;
               break;
          case CE_VIM_MODE_INSERT:
               vim_mode_string = "I";
               vim_mode_fg = COLOR_GREEN;
               break;
          case CE_VIM_MODE_VISUAL:
               vim_mode_string = "V";
               vim_mode_fg = COLOR_YELLOW;
               break;
          case CE_VIM_MODE_VISUAL_LINE:
               vim_mode_string = "VL";
               vim_mode_fg = COLOR_BRIGHT_YELLOW;
               break;
          case CE_VIM_MODE_VISUAL_BLOCK:
               vim_mode_string = "VB";
               vim_mode_fg = COLOR_BRIGHT_YELLOW;
               break;
          case CE_VIM_MODE_REPLACE:
               vim_mode_string = "R";
               vim_mode_fg = COLOR_RED;
               break;
          }
     }

     if(vim_mode_string){
          color_pair = ce_color_def_get(color_defs, vim_mode_fg, COLOR_BRIGHT_BLACK);
          attron(COLOR_PAIR(color_pair));
          mvprintw(bottom, view->rect.left + 1, "%s", vim_mode_string);

          color_pair = ce_color_def_get(color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
          attron(COLOR_PAIR(color_pair));
          printw(" %s", view->buffer->name);
     }else{
          color_pair = ce_color_def_get(color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
          attron(COLOR_PAIR(color_pair));
          mvprintw(bottom, view->rect.left + 1, "%s", view->buffer->name);
     }

     if(view->buffer->status == CE_BUFFER_STATUS_MODIFIED ||
        view->buffer->status == CE_BUFFER_STATUS_NEW_FILE){
          addch('*');
     }else if(view->buffer->status == CE_BUFFER_STATUS_READONLY){
          printw("[RO]");
     }

     if(vim_mode_string && ce_macros_is_recording(macros)){
          printw(" RECORDING %c", macros->recording);
     }

     char cursor_pos_string[32];
     int64_t cursor_pos_string_len = snprintf(cursor_pos_string, 32, "%ld, %ld", view->cursor.x + 1, view->cursor.y + 1);
     mvprintw(bottom, view->rect.right - (cursor_pos_string_len + 1), "%s", cursor_pos_string);
}

CeDestination_t scan_line_for_destination(const char* line){
     CeDestination_t destination = {};
     destination.point = (CePoint_t){-1, -1};

     // grep/gcc format
     char* file_end = strchr(line, ':');
     if(!file_end) return destination;
     char* row_end = strchr(file_end + 1, ':');
     char* col_end = NULL;
     if(row_end) col_end = strchr(row_end + 1, ':');
     // col_end and row_end is not always present

     int64_t filepath_len = file_end - line;
     if(filepath_len >= PATH_MAX) return destination;
     strncpy(destination.filepath, line, filepath_len);
     destination.filepath[filepath_len] = 0;
     char* end = NULL;

     if(row_end){
          destination.point.y = strtol(file_end + 1, &end, 10);
          if(col_end){
               destination.point.x = strtol(row_end + 1, &end, 10);
          }else{
               destination.point.x = 0;
          }
     }else{
          destination.point = (CePoint_t){0, 0};
     }

     return destination;
}

void draw_layout(CeLayout_t* layout, CeVim_t* vim, CeMacros_t* macros, CeTerminal_t* terminal,
                 CeColorDefs_t* color_defs, int64_t tab_width, CeLayout_t* current, CeSyntaxDef_t* syntax_defs,
                 int64_t terminal_width){
     switch(layout->type){
     default:
          break;
     case CE_LAYOUT_TYPE_VIEW:
     {
          CeDrawColorList_t draw_color_list = {};
          if(layout->view.buffer == terminal->lines_buffer || layout->view.buffer == terminal->alternate_lines_buffer){
               layout->view.buffer = terminal->buffer;
               syntax_highlight_terminal(&layout->view, terminal, &draw_color_list, syntax_defs);
          }else{
               ce_syntax_highlight_c(&layout->view, layout == current ? vim : NULL, &draw_color_list, syntax_defs);
          }
          draw_view(&layout->view, tab_width, &draw_color_list, color_defs);
          ce_draw_color_list_free(&draw_color_list);
          draw_view_status(&layout->view, layout == current ? vim : NULL, macros, color_defs, 0);
          int64_t rect_height = layout->view.rect.bottom - layout->view.rect.top;
          int color_pair = ce_color_def_get(color_defs, COLOR_BRIGHT_BLACK, COLOR_BRIGHT_BLACK);
          attron(COLOR_PAIR(color_pair));
          if(layout->view.rect.right < (terminal_width - 1)){
               for(int i = 0; i < rect_height; i++){
                    mvaddch(layout->view.rect.top + i, layout->view.rect.right, ' ');
               }
          }
     } break;
     case CE_LAYOUT_TYPE_LIST:
          for(int64_t i = 0; i < layout->list.layout_count; i++){
               draw_layout(layout->list.layouts[i], vim, macros, terminal, color_defs, tab_width, current, syntax_defs, terminal_width);
          }
          break;
     case CE_LAYOUT_TYPE_TAB:
          draw_layout(layout->tab.root, vim, macros, terminal, color_defs, tab_width, current, syntax_defs, terminal_width);
          break;
     }
}

static CePoint_t view_cursor_on_screen(CeView_t* view, int64_t tab_width){
     // move the visual cursor to the right location
     int64_t visible_cursor_x = 0;
     if(ce_buffer_point_is_valid(view->buffer, view->cursor)){
          visible_cursor_x = ce_util_string_index_to_visible_index(view->buffer->lines[view->cursor.y],
                                                                   view->cursor.x, tab_width);
     }

     return (CePoint_t){visible_cursor_x - view->scroll.x + view->rect.left,
                        view->cursor.y - view->scroll.y + view->rect.top};
}

void* draw_thread(void* thread_data){
     App_t* app = (App_t*)(thread_data);
     struct timeval previous_draw_time;
     struct timeval current_draw_time;
     uint64_t time_since_last_draw = 0;
     CeColorDefs_t color_defs = {};

     gettimeofday(&previous_draw_time, NULL);

     while(!app->quit){
          while(true){
               gettimeofday(&current_draw_time, NULL);
               time_since_last_draw = (current_draw_time.tv_sec - previous_draw_time.tv_sec) * 1000000LL +
                                      (current_draw_time.tv_usec - previous_draw_time.tv_usec);
               if(time_since_last_draw >= DRAW_USEC_LIMIT){
                    if(app->ready_to_draw){
                         app->ready_to_draw = false;
                         break;
                    }else if(app->terminal.ready_to_draw){
                         app->terminal.ready_to_draw = false;
                         break;
                    }
               }
               sleep(0);
          }

          previous_draw_time = current_draw_time;

          //erase();

          CeLayout_t* tab_list_layout = app->tab_list_layout;
          CeLayout_t* tab_layout = tab_list_layout->tab_list.current;

          // draw a tab bar if there is more than 1 tab
          if(tab_list_layout->tab_list.tab_count > 1){
               move(0, 0);
               int color_pair = ce_color_def_get(&color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
               attron(COLOR_PAIR(color_pair));
               for(int64_t i = tab_list_layout->tab_list.rect.left; i <= tab_list_layout->tab_list.rect.right; i++){
                    addch(' ');
               }

               move(0, 0);

               for(int64_t i = 0; i < tab_list_layout->tab_list.tab_count; i++){
                    if(tab_list_layout->tab_list.tabs[i] == tab_list_layout->tab_list.current){
                         color_pair = ce_color_def_get(&color_defs, COLOR_BRIGHT_WHITE, COLOR_DEFAULT);
                         attron(COLOR_PAIR(color_pair));
                    }else{
                         color_pair = ce_color_def_get(&color_defs, COLOR_DEFAULT, COLOR_BRIGHT_BLACK);
                         attron(COLOR_PAIR(color_pair));
                    }

                    if(tab_list_layout->tab_list.tabs[i]->tab.current->type == CE_LAYOUT_TYPE_VIEW){
                         const char* buffer_name = tab_list_layout->tab_list.tabs[i]->tab.current->view.buffer->name;

                         printw(" %s ", buffer_name);
                    }else{
                         printw(" selection ");
                    }
               }
          }

          standend();
          draw_layout(tab_layout, &app->vim, &app->macros, &app->terminal, &color_defs, app->config_options.tab_width,
                      tab_layout->tab.current, app->syntax_defs, tab_list_layout->tab_list.rect.right);

          if(app->input_mode){
               CeDrawColorList_t draw_color_list = {};
               draw_view(&app->input_view, app->config_options.tab_width, &draw_color_list, &color_defs);
               int64_t new_status_bar_offset = (app->input_view.rect.bottom - app->input_view.rect.top) + 1;
               draw_view_status(&app->input_view, &app->vim, &app->macros, &color_defs, 0);
               draw_view_status(&tab_layout->tab.current->view, NULL, &app->macros, &color_defs, -new_status_bar_offset);
          }

          CeComplete_t* complete = app_is_completing(app);
          if(complete && tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW && app->complete_list_buffer->line_count){
               CeLayout_t* view_layout = tab_layout->tab.current;
               app->complete_view.rect.left = view_layout->view.rect.left;
               app->complete_view.rect.right = view_layout->view.rect.right;
               if(app->input_mode){
                    app->complete_view.rect.bottom = app->input_view.rect.top;
               }else{
                    app->complete_view.rect.bottom = view_layout->view.rect.bottom - 1;
               }
               app->complete_view.rect.top = app->complete_view.rect.bottom - app->complete_list_buffer->line_count;
               if(app->complete_view.rect.top <= view_layout->view.rect.top){
                    app->complete_view.rect.top = view_layout->view.rect.top + 1; // account for current view's status bar
               }
               app->complete_view.buffer = app->complete_list_buffer;
               app->complete_view.cursor.y = app->complete_list_buffer->cursor_save.y;
               app->complete_view.cursor.x = 0;
               ce_view_follow_cursor(&app->complete_view, 1, 1, 1); // NOTE: I don't think anyone wants their settings applied here
               CeDrawColorList_t draw_color_list = {};
               draw_view(&app->complete_view, app->config_options.tab_width, &draw_color_list, &color_defs);
               if(app->input_mode){
                    int64_t new_status_bar_offset = (app->complete_view.rect.bottom - app->complete_view.rect.top) + 2;
                    draw_view_status(&tab_layout->tab.current->view, NULL, &app->macros, &color_defs, -new_status_bar_offset);
               }
          }

          // show border when non view is selected
          if(tab_layout->tab.current->type != CE_LAYOUT_TYPE_VIEW){
               int64_t rect_height = 0;
               int64_t rect_width = 0;
               CeRect_t* rect = NULL;
               switch(tab_layout->tab.current->type){
               default:
                    break;
               case CE_LAYOUT_TYPE_LIST:
                    rect = &tab_layout->tab.current->list.rect;
                    rect_width = rect->right - rect->left;
                    rect_height = rect->bottom - rect->top;
                    break;
               case CE_LAYOUT_TYPE_TAB:
                    rect = &tab_layout->tab.current->tab.rect;
                    rect_width = rect->right - rect->left;
                    rect_height = rect->bottom - rect->top;
                    break;
               }

               int color_pair = ce_color_def_get(&color_defs, COLOR_BRIGHT_WHITE, COLOR_DEFAULT);
               attron(COLOR_PAIR(color_pair));
               for(int i = 1; i < rect_height - 1; i++){
                    mvaddch(rect->top + i, rect->right, ACS_VLINE);
                    mvaddch(rect->top + i, rect->left, ACS_VLINE);
               }

               for(int i = 1; i < rect_width - 1; i++){
                    mvaddch(rect->top, rect->left + i, ACS_HLINE);
                    mvaddch(rect->bottom, rect->left + i, ACS_HLINE);
               }

               move(0, 0);
          }else if(app->input_mode){
               CePoint_t screen_cursor = view_cursor_on_screen(&app->input_view, app->config_options.tab_width);
               move(screen_cursor.y, screen_cursor.x);
          }else{
               CeView_t* view = &tab_layout->tab.current->view;

               if(view->buffer == app->terminal.buffer && app->vim.mode == CE_VIM_MODE_INSERT){
                    view->cursor.x = app->terminal.cursor.x;
                    view->cursor.y = app->terminal.cursor.y;
               }

               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width);
               move(screen_cursor.y, screen_cursor.x);
          }

          refresh();
     }

     return NULL;
}

void input_view_overlay(CeView_t* input_view, CeView_t* view){
     input_view->rect.left = view->rect.left;
     input_view->rect.right = view->rect.right;
     input_view->rect.bottom = view->rect.bottom;
     int64_t max_height = (view->rect.bottom - view->rect.top) - 1;
     int64_t height = input_view->buffer->line_count;
     if(height <= 0) height = 1;
     if(height > max_height) height = max_height;
     input_view->rect.top = view->rect.bottom - height;

}

bool enable_input_mode(CeView_t* input_view, CeView_t* view, CeVim_t* vim, const char* dialogue){
     // update input view to overlay the current view
     input_view_overlay(input_view, view);

     // update name based on dialog
     bool success = ce_buffer_alloc(input_view->buffer, 1, dialogue);
     input_view->cursor = (CePoint_t){0, 0};
     vim->mode = CE_VIM_MODE_INSERT;

     return success;
}

void buffer_replace_all(CeBuffer_t* buffer, CePoint_t cursor, const char* match, const char* replacement, CePoint_t start, CePoint_t end,
                        bool regex_search){
     bool chain_undo = false;
     int64_t match_len = 0;
     if(!regex_search) match_len = strlen(match);
     regex_t regex = {};
     int rc = regcomp(&regex, match, REG_EXTENDED);
     if(rc != 0){
          char error_buffer[BUFSIZ];
          regerror(rc, &regex, error_buffer, BUFSIZ);
          ce_log("regcomp() failed: '%s'", error_buffer);
          return;
     }
     while(true){
          CePoint_t match_point;

          // find the match
          if(regex_search){
               CeRegexSearchResult_t result = ce_buffer_regex_search_forward(buffer, start, &regex);
               match_point = result.point;
               match_len = result.length;
          }else{
               match_point = ce_buffer_search_forward(buffer, start, match);
          }

          if(match_point.x < 0) break;
          if(ce_point_after(match_point, end)) break;

          // delete match
          char* removed_string = ce_buffer_dupe_string(buffer, match_point, match_len);
          if(!ce_buffer_remove_string(buffer, match_point, match_len)){
               free(removed_string);
               break;
          }

          CeBufferChange_t change = {};
          change.chain = chain_undo;
          change.insertion = false;
          change.string = removed_string;
          change.location = match_point;
          change.cursor_before = cursor;
          change.cursor_after = cursor;
          ce_buffer_change(buffer, &change);

          chain_undo = true;

          // insert match
          if(!ce_buffer_insert_string(buffer, replacement, match_point)){
               break;
          }

          // commit the insertion
          change.chain = chain_undo;
          change.insertion = true;
          change.string = strdup(replacement);
          change.location = match_point;
          change.cursor_before = cursor;
          change.cursor_after = cursor;
          ce_buffer_change(buffer, &change);
     }
}

void replace_all(CeView_t* view, CeVim_t* vim, const char* match, const char* replace){
     CePoint_t start;
     CePoint_t end;
     if(vim->mode == CE_VIM_MODE_VISUAL){
          if(ce_point_after(view->cursor, vim->visual)){
               start = vim->visual;
               end = view->cursor;
          }else{
               start = view->cursor;
               end = vim->visual;
          }
     }else if(vim->mode == CE_VIM_MODE_VISUAL_LINE){
          if(ce_point_after(view->cursor, vim->visual)){
               start = (CePoint_t){0, vim->visual.y};
               end = (CePoint_t){ce_utf8_last_index(view->buffer->lines[view->cursor.y]), view->cursor.y};
          }else{
               start = (CePoint_t){0, view->cursor.y};
               end = (CePoint_t){ce_utf8_last_index(view->buffer->lines[vim->visual.y]), vim->visual.y};
          }
     }else{
          start = view->cursor;
          end = (CePoint_t){0, view->buffer->line_count};
          if(end.y > 0){
               end.y--;
               end.x = ce_utf8_last_index(view->buffer->lines[end.y]);
          }
     }

     if(ce_point_after(end, start)){
          buffer_replace_all(view->buffer, view->cursor, match, replace, start, end, false);
     }
}

CeCommandStatus_t command_quit(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     bool unsaved_buffers = false;
     BufferNode_t* itr = app->buffer_node_head;
     while(itr){
          if(itr->buffer->status == CE_BUFFER_STATUS_MODIFIED && itr->buffer != app->input_view.buffer){
               unsaved_buffers = true;
               break;
          }
          itr = itr->next;
     }

     if(unsaved_buffers){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, UNSAVED_BUFFERS_DIALOGUE);
     }else{
          app->quit = true;
     }

     return CE_COMMAND_SUCCESS;
}

// TODO: is this useful or did I pre-maturely create this
static bool get_view_info_from_tab(CeLayout_t* tab_layout, CeView_t** view, CeRect_t* view_rect){
     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          *view = &tab_layout->tab.current->view;
     }else if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_LIST){
          *view_rect = tab_layout->list.rect;
     }else if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_TAB){
          *view_rect = tab_layout->tab.rect;
     }else{
          return false;
     }

     return true;
}

CeCommandStatus_t command_select_adjacent_layout(CeCommand_t* command, void* user_data)
{
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CePoint_t target;
     CeView_t* view = NULL;
     CeRect_t view_rect = {};
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(!get_view_info_from_tab(tab_layout, &view, &view_rect)){
          assert(!"unknown layout type");
          return CE_COMMAND_FAILURE;
     }

     if(strcmp(command->args[0].string, "up") == 0){
          if(view){
               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width);
               target = (CePoint_t){screen_cursor.x, view->rect.top - 1};
          }else{
               target = (CePoint_t){view_rect.left, view_rect.top - 1};
          }
     }else if(strcmp(command->args[0].string, "down") == 0){
          if(view){
               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width);
               target = (CePoint_t){screen_cursor.x, view->rect.bottom + 1};
          }else{
               target = (CePoint_t){view_rect.left, view_rect.bottom + 1};
          }
     }else if(strcmp(command->args[0].string, "left") == 0){
          if(view){
               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width);
               target = (CePoint_t){view->rect.left - 1, screen_cursor.y};
          }else{
               target = (CePoint_t){view_rect.left - 1, view_rect.top};
          }
     }else if(strcmp(command->args[0].string, "right") == 0){
          if(view){
               CePoint_t screen_cursor = view_cursor_on_screen(view, app->config_options.tab_width);
               target = (CePoint_t){view->rect.right + 1, screen_cursor.y};
          }else{
               target = (CePoint_t){view_rect.right + 1, view_rect.top};
          }
     }else{
          ce_log("unrecognized argument: '%s'\n", command->args[0].string);
          return CE_COMMAND_PRINT_HELP;
     }

     // wrap around
     if(target.x >= app->terminal_width) target.x %= app->terminal_width;
     if(target.x < 0) target.x = app->terminal_width + target.x;
     if(target.y >= app->terminal_height) target.y %= app->terminal_height;
     if(target.y < 0) target.y = app->terminal_height + target.y;

     CeLayout_t* layout = ce_layout_find_at(tab_layout, target);
     if(layout){
          tab_layout->tab.current = layout;
          app->vim.mode = CE_VIM_MODE_NORMAL;
          app->input_mode = false;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_save_buffer(CeCommand_t* command, void* user_data)
{
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
          ce_buffer_save(view->buffer);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_show_buffers(CeCommand_t* command, void* user_data)
{
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
          view_switch_buffer(view, app->buffer_list_buffer, &app->vim, &app->config_options);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_show_yanks(CeCommand_t* command, void* user_data)
{
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
          view_switch_buffer(view, app->yank_list_buffer, &app->vim, &app->config_options);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_split_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
     bool vertical = false;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }

     if(strcmp(command->args[0].string, "vertical") == 0){
          vertical = true;
     }else if(strcmp(command->args[0].string, "horizontal") == 0){
          // pass
     }else{
          ce_log("unrecognized argument '%s'\n", command->args[0]);
          return CE_COMMAND_PRINT_HELP;
     }

     if(view){
          ce_layout_split(tab_layout, vertical);

          view = &tab_layout->tab.current->view;
          ce_layout_distribute_rect(tab_layout, app->terminal_rect, app->config_options.horizontal_scroll_off, app->config_options.vertical_scroll_off,
                                    app->config_options.tab_width);
     }else{
          ce_layout_split(tab_layout, vertical);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_select_parent_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     CeLayout_t* layout = ce_layout_find_parent(tab_layout, tab_layout->tab.current);
     if(layout) tab_layout->tab.current = layout;
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_delete_layout(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeRect_t view_rect = {};
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     // check if this is the only view, and ignore the delete request
     if(app->tab_list_layout->tab_list.tab_count == 1 &&
        tab_layout->tab.root->type == CE_LAYOUT_TYPE_LIST &&
        tab_layout->tab.root->list.layout_count == 1 &&
        tab_layout->tab.current == tab_layout->tab.root->list.layouts[0]){
          return CE_COMMAND_NO_ACTION;
     }

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(!get_view_info_from_tab(tab_layout, &view, &view_rect)){
          assert(!"unknown layout type");
          return CE_COMMAND_FAILURE;
     }

     CePoint_t cursor = {0, 0};
     if(view) cursor = view_cursor_on_screen(view, app->config_options.tab_width);
     ce_layout_delete(tab_layout, tab_layout->tab.current);
     ce_layout_distribute_rect(tab_layout, app->terminal_rect, app->config_options.horizontal_scroll_off,
                               app->config_options.vertical_scroll_off, app->config_options.tab_width);
     CeLayout_t* layout = ce_layout_find_at(tab_layout, cursor);
     if(layout) tab_layout->tab.current = layout;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_load_file(CeCommand_t* command, void* user_data){
     if(command->arg_count < 0 || command->arg_count > 1) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     if(command->arg_count == 1){
          if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;
          load_file_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim, command->args[0].string);
     }else{ // it's 0
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "LOAD FILE");

          char* base_directory = view_base_directory(view, app);
          complete_files(&app->load_file_complete, app->input_view.buffer->lines[0], base_directory);
          free(base_directory);
          build_complete_list(app->complete_list_buffer, &app->load_file_complete);
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_new_tab(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;

     CeLayout_t* new_tab_layout = ce_layout_tab_list_add(app->tab_list_layout);
     if(!new_tab_layout) return CE_COMMAND_NO_ACTION;
     app->tab_list_layout->tab_list.current = new_tab_layout;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_select_adjacent_tab(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;

     if(strcmp(command->args[0].string, "left") == 0){
          for(int64_t i = 0; i < app->tab_list_layout->tab_list.tab_count; i++){
               if(app->tab_list_layout->tab_list.current == app->tab_list_layout->tab_list.tabs[i]){
                    if(i > 0){
                         app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[i - 1];
                         return CE_COMMAND_SUCCESS;
                    }else{
                         // wrap around
                         app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[app->tab_list_layout->tab_list.tab_count - 1];
                         return CE_COMMAND_SUCCESS;
                    }
                    break;
               }
          }
     }else if(strcmp(command->args[0].string, "right") == 0){
          for(int64_t i = 0; i < app->tab_list_layout->tab_list.tab_count; i++){
               if(app->tab_list_layout->tab_list.current == app->tab_list_layout->tab_list.tabs[i]){
                    if(i < (app->tab_list_layout->tab_list.tab_count - 1)){
                         app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[i + 1];
                         return CE_COMMAND_SUCCESS;
                    }else{
                         // wrap around
                         app->tab_list_layout->tab_list.current = app->tab_list_layout->tab_list.tabs[0];
                         return CE_COMMAND_SUCCESS;
                    }
                    break;
               }
          }
     }else{
          ce_log("unrecognized argument: '%s'\n", command->args[0]);
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_NO_ACTION;
}

CeCommandStatus_t command_search(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     if(strcmp(command->args[0].string, "forward") == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "SEARCH");
          app->vim.search_mode = CE_VIM_SEARCH_MODE_FORWARD;
          app->search_start = view->cursor;
     }else if(strcmp(command->args[0].string, "backward") == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "REVERSE SEARCH");
          app->vim.search_mode = CE_VIM_SEARCH_MODE_BACKWARD;
          app->search_start = view->cursor;
     }else{
          ce_log("unrecognized argument: '%s'\n", command->args[0]);
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_regex_search(CeCommand_t* command, void* user_data){
     if(command->arg_count != 1) return CE_COMMAND_PRINT_HELP;
     if(command->args[0].type != CE_COMMAND_ARG_STRING) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     if(strcmp(command->args[0].string, "forward") == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "REGEX SEARCH");
          app->vim.search_mode = CE_VIM_SEARCH_MODE_REGEX_FORWARD;
          app->search_start = view->cursor;
     }else if(strcmp(command->args[0].string, "backward") == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "REGEX REVERSE SEARCH");
          app->vim.search_mode = CE_VIM_SEARCH_MODE_REGEX_BACKWARD;
          app->search_start = view->cursor;
     }else{
          ce_log("unrecognized argument: '%s'\n", command->args[0]);
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

// lol
CeCommandStatus_t command_command(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "COMMAND");
     build_complete_list(app->complete_list_buffer, &app->command_complete);
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_switch_to_terminal(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     CeLayout_t* terminal_layout = ce_layout_buffer_in_view(tab_layout, app->terminal.buffer);
     if(terminal_layout){
          tab_layout->tab.current = terminal_layout;
     }else{
          view_switch_buffer(view, app->terminal.buffer, &app->vim, &app->config_options);
     }

     app->vim.mode = CE_VIM_MODE_INSERT;

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_switch_buffer(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;

     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "SWITCH BUFFER");

     int64_t buffer_count = 0;
     BufferNode_t* itr = app->buffer_node_head;
     while(itr){
          buffer_count++;
          itr = itr->next;
     }

     char** filenames = malloc(buffer_count * sizeof(*filenames));

     int64_t index = 0;
     itr = app->buffer_node_head;
     while(itr){
          filenames[index] = strdup(itr->buffer->name);
          index++;
          itr = itr->next;
     }

     ce_complete_init(&app->switch_buffer_complete, (const char**)filenames, buffer_count);
     build_complete_list(app->complete_list_buffer, &app->switch_buffer_complete);

     for(int64_t i = 0; i < buffer_count; i++){
          free(filenames[i]);
     }
     free(filenames);

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_redraw(CeCommand_t* command, void* user_data){
     clear();
     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_goto_destination_in_line(CeCommand_t* command, void* user_data){
     if(command->arg_count != 0) return CE_COMMAND_PRINT_HELP;
     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     if(view->buffer->line_count == 0) return CE_COMMAND_NO_ACTION;

     CeDestination_t destination = scan_line_for_destination(view->buffer->lines[view->cursor.y]);
     if(destination.point.x < 0) return CE_COMMAND_NO_ACTION;

     CeBuffer_t* buffer = load_file_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim, destination.filepath);
     if(!buffer) return CE_COMMAND_NO_ACTION;

     if(destination.point.y < buffer->line_count){
          view->cursor.y = destination.point.y;
          int64_t line_len = ce_utf8_strlen(buffer->lines[view->cursor.y]);
          if(destination.point.x < line_len) view->cursor.x = destination.point.x;
     }

     return CE_COMMAND_SUCCESS;
}

CeCommandStatus_t command_replace_all(CeCommand_t* command, void* user_data){
     App_t* app = user_data;
     CeView_t* view = NULL;
     CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;

     if(app->input_mode) return CE_COMMAND_NO_ACTION;

     if(tab_layout->tab.current->type == CE_LAYOUT_TYPE_VIEW){
          view = &tab_layout->tab.current->view;
     }else{
          return CE_COMMAND_NO_ACTION;
     }

     if(command->arg_count == 0){
          app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "REPLACE ALL");
     }else if(command->arg_count == 1 && command->args[0].type == CE_COMMAND_ARG_STRING){
          int64_t index = ce_vim_yank_register_index('/');
          CeVimYank_t* yank = app->vim.yanks + index;
          if(yank->text){
               replace_all(view, &app->vim, yank->text, command->args[0].string);
          }
     }else{
          return CE_COMMAND_PRINT_HELP;
     }

     return CE_COMMAND_SUCCESS;
}

static int int_strneq(int* a, int* b, size_t len)
{
     for(size_t i = 0; i < len; ++i){
          if(!*a) return false;
          if(!*b) return false;
          if(*a != *b) return false;
          a++;
          b++;
     }

     return true;
}

void app_handle_key(App_t* app, CeView_t* view, int key){
     if(app->key_count == 0 &&
        app->last_vim_handle_result != CE_VIM_PARSE_IN_PROGRESS &&
        app->last_vim_handle_result != CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY &&
        app->last_vim_handle_result != CE_VIM_PARSE_CONTINUE &&
        app->vim.mode != CE_VIM_MODE_INSERT){
          if(key == 'q' && !app->replay_macro){ // TODO: make configurable
               if(ce_macros_is_recording(&app->macros)){
                    ce_macros_end_recording(&app->macros);
                    app->record_macro = false;
               }else{
                    app->record_macro = true;
               }

               return;
          }

          if(key == '@' && !app->record_macro && !app->replay_macro){
               app->replay_macro = true;
               return;
          }

          if(app->record_macro && !ce_macros_is_recording(&app->macros)){
               ce_macros_begin_recording(&app->macros, key);
               return;
          }

          if(app->replay_macro){
               app->replay_macro = false;
               CeRune_t* rune_string = ce_macros_get_register_string(&app->macros, key);
               if(rune_string){
                    CeRune_t* itr = rune_string;
                    while(*itr){
                         app_handle_key(app, view, *itr);
                         itr++;
                    }
               }

               free(rune_string);
               return;
          }
     }

     if(ce_macros_is_recording(&app->macros)){
          ce_macros_record_key(&app->macros, key);
     }

     if(view && (view->buffer == app->terminal.lines_buffer || view->buffer == app->terminal.alternate_lines_buffer) &&
        app->vim.mode == CE_VIM_MODE_INSERT && !app->input_mode){
          if(key == KEY_ESCAPE){
               app->vim.mode = CE_VIM_MODE_NORMAL;
          }else if(key == 1){ // ctrl + a
               // TODO: make this configurable
               // send escape
               ce_terminal_send_key(&app->terminal, KEY_ESCAPE);
          }else{
               ce_terminal_send_key(&app->terminal, key);
          }
          return;
     }

     // as long as vim isn't in the middle of handling keys, in insert mode vim returns VKH_HANDLED_KEY TODO: is that what we want?
     if(app->last_vim_handle_result != CE_VIM_PARSE_IN_PROGRESS &&
        app->last_vim_handle_result != CE_VIM_PARSE_CONSUME_ADDITIONAL_KEY &&
        app->last_vim_handle_result != CE_VIM_PARSE_CONTINUE &&
        app->vim.mode != CE_VIM_MODE_INSERT){
          // append to keys
          if(app->key_count < APP_MAX_KEY_COUNT){
               app->keys[app->key_count] = key;
               app->key_count++;

               bool no_matches = true;
               for(int64_t i = 0; i < app->key_binds[app->vim.mode].count; ++i){
                    if(int_strneq(app->key_binds[app->vim.mode].binds[i].keys, app->keys, app->key_count)){
                         no_matches = false;
                         // if we have matches, but don't completely match, then wait for more keypresses,
                         // otherwise, execute the action
                         if(app->key_binds[app->vim.mode].binds[i].key_count == app->key_count){
                              CeCommand_t* command = &app->key_binds[app->vim.mode].binds[i].command;
                              CeCommandFunc_t* command_func = NULL;
                              CeCommandEntry_t* entry = NULL;
                              for(int64_t c = 0; c < app->command_entry_count; ++c){
                                   entry = app->command_entries + c;
                                   if(strcmp(entry->name, command->name) == 0){
                                        command_func = entry->func;
                                        break;
                                   }
                              }

                              if(command_func){
                                   CeCommandStatus_t cs = command_func(command, app);

                                   switch(cs){
                                   default:
                                        app->key_count = 0;
                                        return;
                                   case CE_COMMAND_NO_ACTION:
                                        break;
                                   case CE_COMMAND_FAILURE:
                                        ce_log("'%s' failed", entry->name);
                                        break;
                                   case CE_COMMAND_PRINT_HELP:
                                        ce_log("command help:\n'%s' %s\n", entry->name, entry->description);
                                        break;
                                   }
                              }else{
                                   ce_log("unknown command: '%s'", command->name);
                              }

                              app->key_count = 0;
                         }else{
                              return;
                         }
                    }
               }

               if(no_matches){
                    app->vim.current_command[0] = 0;
                    for(int64_t i = 0; i < app->key_count - 1; ++i){
                         ce_vim_append_key(&app->vim, app->keys[i]);
                    }

                    app->key_count = 0;
               }
          }
     }

     if(view){
          if(key == KEY_ENTER){
               if(view->buffer == app->buffer_list_buffer){
                    BufferNode_t* itr = app->buffer_node_head;
                    int64_t index = 0;
                    while(itr){
                         if(index == view->cursor.y){
                              view_switch_buffer(view, itr->buffer, &app->vim, &app->config_options);
                              break;
                         }
                         itr = itr->next;
                         index++;
                    }
               }else if(!app->input_mode && view->buffer == app->yank_list_buffer){
                    // TODO: move to command
                    app->edit_register = -1;
                    int64_t line = view->cursor.y;
                    CeVimYank_t* selected_yank = NULL;
                    for(int64_t i = 0; i < CE_ASCII_PRINTABLE_CHARACTERS; i++){
                         CeVimYank_t* yank = app->vim.yanks + i;
                         if(yank->text != NULL){
                              int64_t line_count = 2;
                              line_count += ce_util_count_string_lines(yank->text);
                              line -= line_count;
                              if(line <= 0){
                                   app->edit_register = i;
                                   selected_yank = yank;
                                   break;
                              }
                         }
                    }

                    if(app->edit_register >= 0){
                         app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "EDIT YANK");
                         ce_buffer_insert_string(app->input_view.buffer, selected_yank->text, (CePoint_t){0, 0});
                         app->input_view.cursor.y = app->input_view.buffer->line_count;
                         if(app->input_view.cursor.y) app->input_view.cursor.y--;
                         app->input_view.cursor.x = ce_utf8_strlen(app->input_view.buffer->lines[app->input_view.cursor.y]);
                    }
               }else if(!app->input_mode && view->buffer == app->macro_list_buffer){
                    // TODO: move to command
                    app->edit_register = -1;
                    int64_t line = view->cursor.y;
                    char* macro_string = NULL;
                    for(int64_t i = 0; i < CE_ASCII_PRINTABLE_CHARACTERS; i++){
                         CeRuneNode_t* rune_node = app->macros.rune_head[i];
                         if(rune_node){
                              line -= 2;
                              if(line <= 2){
                                   app->edit_register = i;
                                   CeRune_t* rune_string = ce_rune_node_string(rune_node);
                                   macro_string = ce_rune_string_to_char_string(rune_string);
                                   free(rune_string);
                                   break;
                              }
                         }
                    }

                    if(app->edit_register >= 0){
                         app->input_mode = enable_input_mode(&app->input_view, view, &app->vim, "EDIT MACRO");
                         ce_buffer_insert_string(app->input_view.buffer, macro_string, (CePoint_t){0, 0});
                         app->input_view.cursor.y = app->input_view.buffer->line_count;
                         if(app->input_view.cursor.y) app->input_view.cursor.y--;
                         app->input_view.cursor.x = ce_utf8_strlen(app->input_view.buffer->lines[app->input_view.cursor.y]);
                         free(macro_string);
                    }
               }else if(app->input_mode){
                    if(app->input_view.buffer->line_count && strlen(app->input_view.buffer->lines[0])){
                         if(strcmp(app->input_view.buffer->name, "LOAD FILE") == 0){
                              char* base_directory = view_base_directory(view, app);
                              char filepath[PATH_MAX];
                              for(int64_t i = 0; i < app->input_view.buffer->line_count; i++){
                                   if(base_directory){
                                        snprintf(filepath, PATH_MAX, "%s/%s", base_directory, app->input_view.buffer->lines[i]);
                                   }else{
                                        strncpy(filepath, app->input_view.buffer->lines[i], PATH_MAX);
                                   }
                                   load_file_into_view(&app->buffer_node_head, view, &app->config_options, &app->vim, filepath);
                              }

                              free(base_directory);
                         }else if(strcmp(app->input_view.buffer->name, "SEARCH") == 0 ||
                                  strcmp(app->input_view.buffer->name, "REVERSE SEARCH") == 0 ||
                                  strcmp(app->input_view.buffer->name, "REGEX SEARCH") == 0 ||
                                  strcmp(app->input_view.buffer->name, "REGEX REVERSE SEARCH") == 0){
                              // update yanks
                              int64_t index = ce_vim_yank_register_index('/');
                              CeVimYank_t* yank = app->vim.yanks + index;
                              free(yank->text);
                              yank->text = strdup(app->input_view.buffer->lines[0]);
                              yank->line = false;
                         }else if(strcmp(app->input_view.buffer->name, "COMMAND") == 0){
                              char* end_of_number = app->input_view.buffer->lines[0];
                              int64_t line_number = strtol(app->input_view.buffer->lines[0], &end_of_number, 10);
                              if(end_of_number > app->input_view.buffer->lines[0]){
                                   // if the command entered was a number, go to that line
                                   if(line_number >= 0 && line_number < view->buffer->line_count){
                                        view->cursor.y = line_number - 1;
                                        view->cursor.x = ce_vim_soft_begin_line(view->buffer, view->cursor.y);
                                        // TODO: compress with other view centering code I've seen
                                        int64_t view_height = view->rect.bottom - view->rect.top;
                                        ce_view_follow_cursor(view, app->config_options.horizontal_scroll_off,
                                                              app->config_options.vertical_scroll_off,
                                                              app->config_options.tab_width);
                                        ce_view_scroll_to(view, (CePoint_t){view->cursor.x, view->cursor.y - (view_height / 2)});
                                   }
                              }else{
                                   // convert and run the command
                                   CeCommand_t command = {};
                                   if(!ce_command_parse(&command, app->input_view.buffer->lines[0])){
                                        ce_log("failed to parse command: '%s'\n", app->input_view.buffer->lines[0]);
                                   }else{
                                        CeCommandFunc_t* command_func = NULL;
                                        for(int64_t i = 0; i < app->command_entry_count; i++){
                                             CeCommandEntry_t* entry = app->command_entries + i;
                                             if(strcmp(entry->name, command.name) == 0){
                                                  command_func = entry->func;
                                                  break;
                                             }
                                        }

                                        if(command_func){
                                             // TODO: compress this, we do it a lot, and I'm sure there will be more we need to do in the future
                                             app->input_mode = false;
                                             app->vim.mode = CE_VIM_MODE_NORMAL;

                                             command_func(&command, app);

                                             return;
                                        }else{
                                             ce_log("unknown command: '%s'\n", command.name);
                                        }
                                   }
                              }
                         }else if(strcmp(app->input_view.buffer->name, "EDIT YANK") == 0){
                              CeVimYank_t* yank = app->vim.yanks + app->edit_register;
                              free(yank->text);
                              yank->text = ce_buffer_dupe(app->input_view.buffer);
                              yank->line = false;
                         }else if(strcmp(app->input_view.buffer->name, "EDIT MACRO") == 0){
                              CeRune_t* rune_string = ce_char_string_to_rune_string(app->input_view.buffer->lines[0]);
                              if(rune_string){
                                   ce_rune_node_free(app->macros.rune_head + app->edit_register);
                                   CeRune_t* itr = rune_string;
                                   while(*itr){
                                        ce_rune_node_insert(app->macros.rune_head + app->edit_register, *itr);
                                        itr++;
                                   }

                                   free(rune_string);
                              }
                         }else if(strcmp(app->input_view.buffer->name, "SWITCH BUFFER") == 0){
                              BufferNode_t* itr = app->buffer_node_head;
                              while(itr){
                                   if(strcmp(itr->buffer->name, app->input_view.buffer->lines[0]) == 0){
                                        view_switch_buffer(view, itr->buffer, &app->vim, &app->config_options);
                                        break;
                                   }
                                   itr = itr->next;
                              }
                         }else if(strcmp(app->input_view.buffer->name, UNSAVED_BUFFERS_DIALOGUE) == 0){
                              if(strcmp(app->input_view.buffer->lines[0], "y") == 0 ||
                                 strcmp(app->input_view.buffer->lines[0], "Y") == 0){
                                   app->quit = true;
                              }
                         }else if(strcmp(app->input_view.buffer->name, "REPLACE ALL") == 0){
                              int64_t index = ce_vim_yank_register_index('/');
                              CeVimYank_t* yank = app->vim.yanks + index;
                              if(yank->text){
                                   replace_all(view, &app->vim, yank->text, app->input_view.buffer->lines[0]);
                              }
                         }
                    }

                    // TODO: compress this, we do it a lot, and I'm sure there will be more we need to do in the future
                    app->input_mode = false;
                    app->vim.mode = CE_VIM_MODE_NORMAL;
                    return;
               }else{
                    key = CE_NEWLINE;
               }
          }else if(key == CE_TAB){ // TODO: configure auto complete key?
               CeComplete_t* complete = app_is_completing(app);
               if(app->vim.mode == CE_VIM_MODE_INSERT && complete){
                    if(complete->current >= 0){
                         char* insertion = strdup(complete->elements[complete->current].string);
                         int64_t insertion_len = strlen(insertion);
                         int64_t delete_len = strlen(complete->current_match);
                         CePoint_t delete_point = ce_buffer_advance_point(app->input_view.buffer, app->input_view.cursor, -delete_len);
                         if(delete_len > 0 && ce_buffer_remove_string(app->input_view.buffer, delete_point, delete_len)){
                              CeBufferChange_t change = {};
                              change.chain = true;
                              change.insertion = false;
                              change.string = strdup(complete->current_match);
                              change.location = delete_point;
                              change.cursor_before = app->input_view.cursor;
                              change.cursor_after = app->input_view.cursor;
                              ce_buffer_change(view->buffer, &change);
                         }

                         if(insertion_len > 0 && ce_buffer_insert_string(app->input_view.buffer, insertion, delete_point)){
                              CePoint_t new_cursor = ce_buffer_advance_point(app->input_view.buffer, delete_point, insertion_len);
                              CeBufferChange_t change = {};
                              change.chain = true;
                              change.insertion = true;
                              change.string = insertion;
                              change.location = delete_point;
                              change.cursor_before = app->input_view.cursor;
                              change.cursor_after = new_cursor;
                              ce_buffer_change(view->buffer, &change);
                              app->input_view.cursor = new_cursor;
                         }

                         // if completion was load_file, continue auto completing since we could have completed a directory
                         // and want to see what is in it
                         if(complete == &app->load_file_complete){
                              char* base_directory = view_base_directory(view, app);
                              complete_files(&app->load_file_complete, app->input_view.buffer->lines[0], base_directory);
                              free(base_directory);
                              build_complete_list(app->complete_list_buffer, &app->load_file_complete);
                         }
                    }

                    return;
               }
          }else if(key == 14){ // ctrl + n
               CeComplete_t* complete = app_is_completing(app);
               if(app->vim.mode == CE_VIM_MODE_INSERT && complete){
                    ce_complete_next_match(complete);
                    build_complete_list(app->complete_list_buffer, complete);
                    return;
               }
          }else if(key == 16){ // ctrl + p
               CeComplete_t* complete = app_is_completing(app);
               if(app->vim.mode == CE_VIM_MODE_INSERT && complete){
                    ce_complete_previous_match(complete);
                    build_complete_list(app->complete_list_buffer, complete);
                    return;
               }
          }else if(key == KEY_ESCAPE && app->input_mode && app->vim.mode == CE_VIM_MODE_NORMAL){ // Escape
               app->input_mode = false;
               app->vim.mode = CE_VIM_MODE_NORMAL;

               CeComplete_t* complete = app_is_completing(app);
               if(complete) ce_complete_reset(complete);
               return;
          }

          if(app->input_mode){
               app->last_vim_handle_result = ce_vim_handle_key(&app->vim, &app->input_view, key, &app->config_options);

               if(app->vim.mode == CE_VIM_MODE_INSERT && app->input_view.buffer->line_count){
                    if(strcmp(app->input_view.buffer->name, "COMMAND") == 0){
                         ce_complete_match(&app->command_complete, app->input_view.buffer->lines[0]);
                         build_complete_list(app->complete_list_buffer, &app->command_complete);
                    }else if(strcmp(app->input_view.buffer->name, "LOAD FILE") == 0){
                         char* base_directory = view_base_directory(view, app);
                         complete_files(&app->load_file_complete, app->input_view.buffer->lines[0], base_directory);
                         free(base_directory);
                         build_complete_list(app->complete_list_buffer, &app->load_file_complete);
                    }else if(strcmp(app->input_view.buffer->name, "SWITCH BUFFER") == 0){
                         ce_complete_match(&app->switch_buffer_complete, app->input_view.buffer->lines[0]);
                         build_complete_list(app->complete_list_buffer, &app->switch_buffer_complete);
                    }
               }
          }else{
               app->last_vim_handle_result = ce_vim_handle_key(&app->vim, view, key, &app->config_options);
          }
     }else{
          if(key == KEY_ESCAPE){
               CeLayout_t* tab_layout = app->tab_list_layout->tab_list.current;
               CeLayout_t* current_layout = tab_layout->tab.current;
               CeRect_t layout_rect = {};

               switch(current_layout->type){
               default:
                    assert(!"unexpected current layout type");
                    return;
               case CE_LAYOUT_TYPE_LIST:
                    layout_rect = current_layout->list.rect;
                    break;
               case CE_LAYOUT_TYPE_TAB:
                    layout_rect = current_layout->tab.rect;
                    break;
               case CE_LAYOUT_TYPE_TAB_LIST:
                    layout_rect = current_layout->tab_list.rect;
                    break;
               }

               tab_layout->tab.current = ce_layout_find_at(tab_layout, (CePoint_t){layout_rect.left, layout_rect.top});
               return;
          }
     }

     // incremental search
     if(app->input_mode){
          if(strcmp(app->input_view.buffer->name, "SEARCH") == 0){
               if(app->input_view.buffer->line_count && view->buffer->line_count && strlen(app->input_view.buffer->lines[0])){
                    CePoint_t match_point = ce_buffer_search_forward(view->buffer, view->cursor, app->input_view.buffer->lines[0]);
                    if(match_point.x >= 0){
                         view->cursor = match_point;
                         CePoint_t before_follow = view->scroll;
                         ce_view_follow_cursor(view, app->config_options.horizontal_scroll_off,
                                               app->config_options.vertical_scroll_off, app->config_options.tab_width);
                         if(!ce_points_equal(before_follow, view->scroll)){
                              int64_t view_height = view->rect.bottom - view->rect.top;
                              ce_view_scroll_to(view, (CePoint_t){0, view->cursor.y - (view_height / 2)});
                         }
                    }else{
                         view->cursor = app->search_start;
                    }
               }else{
                    view->cursor = app->search_start;
               }
          }else if(strcmp(app->input_view.buffer->name, "REVERSE SEARCH") == 0){
               if(app->input_view.buffer->line_count && view->buffer->line_count && strlen(app->input_view.buffer->lines[0])){
                    CePoint_t match_point = ce_buffer_search_backward(view->buffer, view->cursor, app->input_view.buffer->lines[0]);
                    if(match_point.x >= 0){
                         view->cursor = match_point;
                         CePoint_t before_follow = view->scroll;
                         ce_view_follow_cursor(view, app->config_options.horizontal_scroll_off,
                                               app->config_options.vertical_scroll_off, app->config_options.tab_width);
                         if(!ce_points_equal(before_follow, view->scroll)){
                              int64_t view_height = view->rect.bottom - view->rect.top;
                              ce_view_scroll_to(view, (CePoint_t){0, view->cursor.y - (view_height / 2)});
                         }
                    }else{
                         view->cursor = app->search_start;
                    }
               }else{
                    view->cursor = app->search_start;
               }
          }else if(strcmp(app->input_view.buffer->name, "REGEX SEARCH") == 0){
               if(app->input_view.buffer->line_count && view->buffer->line_count && strlen(app->input_view.buffer->lines[0])){
                    regex_t regex = {};
                    int rc = regcomp(&regex, app->input_view.buffer->lines[0], REG_EXTENDED);
                    if(rc != 0){
                         char error_buffer[BUFSIZ];
                         regerror(rc, &regex, error_buffer, BUFSIZ);
                         ce_log("regcomp() failed: '%s'", error_buffer);
                    }else{
                         CeRegexSearchResult_t result = ce_buffer_regex_search_forward(view->buffer, view->cursor, &regex);
                         // TODO: compress result code with all other search code
                         if(result.point.x >= 0){
                              view->cursor = result.point;
                              CePoint_t before_follow = view->scroll;
                              ce_view_follow_cursor(view, app->config_options.horizontal_scroll_off,
                                                    app->config_options.vertical_scroll_off, app->config_options.tab_width);
                              if(!ce_points_equal(before_follow, view->scroll)){
                                   int64_t view_height = view->rect.bottom - view->rect.top;
                                   ce_view_scroll_to(view, (CePoint_t){0, view->cursor.y - (view_height / 2)});
                              }
                         }else{
                              view->cursor = app->search_start;
                         }
                    }
               }else{
                    view->cursor = app->search_start;
               }
          }else if(strcmp(app->input_view.buffer->name, "REGEX REVERSE SEARCH") == 0){
               if(app->input_view.buffer->line_count && view->buffer->line_count && strlen(app->input_view.buffer->lines[0])){
                    regex_t regex = {};
                    int rc = regcomp(&regex, app->input_view.buffer->lines[0], REG_EXTENDED);
                    if(rc != 0){
                         char error_buffer[BUFSIZ];
                         regerror(rc, &regex, error_buffer, BUFSIZ);
                         ce_log("regcomp() failed: '%s'", error_buffer);
                    }else{
                         CeRegexSearchResult_t result = ce_buffer_regex_search_backward(view->buffer, view->cursor, &regex);
                         if(result.point.x >= 0){
                              view->cursor = result.point;
                              CePoint_t before_follow = view->scroll;
                              ce_view_follow_cursor(view, app->config_options.horizontal_scroll_off,
                                                    app->config_options.vertical_scroll_off, app->config_options.tab_width);
                              if(!ce_points_equal(before_follow, view->scroll)){
                                   int64_t view_height = view->rect.bottom - view->rect.top;
                                   ce_view_scroll_to(view, (CePoint_t){0, view->cursor.y - (view_height / 2)});
                              }
                         }else{
                              view->cursor = app->search_start;
                         }
                    }
               }else{
                    view->cursor = app->search_start;
               }
          }
     }
}

int main(int argc, char** argv){
     setlocale(LC_ALL, "");

     if(!ce_log_init("ce.log")){
          return 1;
     }

     // init ncurses
     {
          initscr();
          keypad(stdscr, TRUE);
          raw();
          cbreak();
          noecho();

          if(has_colors() == FALSE){
               printf("Your terminal doesn't support colors. what year do you live in?\n");
               return 1;
          }

          start_color();
          use_default_colors();

          define_key("\x11", KEY_CLOSE);     // ctrl + q    (17) (0x11) ASCII "DC1" Device Control 1
          define_key("\x12", KEY_REDO);
          define_key(NULL, KEY_ENTER);       // Blow away enter
          define_key("\x0D", KEY_ENTER);     // Enter       (13) (0x0D) ASCII "CR"  NL Carriage Return
     }

     // TODO: allocate this on the heap when/if it gets too big?
     App_t app = {};

     // init custon syntax highlighting
     CeSyntaxDef_t syntax_defs[CE_SYNTAX_COLOR_COUNT];
     {
          syntax_defs[CE_SYNTAX_COLOR_NORMAL].fg = COLOR_DEFAULT;
          syntax_defs[CE_SYNTAX_COLOR_NORMAL].bg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_TYPE].fg = COLOR_BRIGHT_BLUE;
          syntax_defs[CE_SYNTAX_COLOR_TYPE].bg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_KEYWORD].fg = COLOR_BLUE;
          syntax_defs[CE_SYNTAX_COLOR_KEYWORD].bg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_CONTROL].fg = COLOR_YELLOW;
          syntax_defs[CE_SYNTAX_COLOR_CONTROL].bg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_CAPS_VAR].fg = COLOR_MAGENTA;
          syntax_defs[CE_SYNTAX_COLOR_CAPS_VAR].bg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_COMMENT].fg = COLOR_GREEN;
          syntax_defs[CE_SYNTAX_COLOR_COMMENT].bg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_STRING].fg = COLOR_RED;
          syntax_defs[CE_SYNTAX_COLOR_STRING].bg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_CHAR_LITERAL].fg = COLOR_RED;
          syntax_defs[CE_SYNTAX_COLOR_CHAR_LITERAL].bg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_NUMBER_LITERAL].fg = COLOR_MAGENTA;
          syntax_defs[CE_SYNTAX_COLOR_NUMBER_LITERAL].bg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_PREPROCESSOR].fg = COLOR_BRIGHT_MAGENTA;
          syntax_defs[CE_SYNTAX_COLOR_PREPROCESSOR].bg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_TRAILING_WHITESPACE].fg = COLOR_RED;
          syntax_defs[CE_SYNTAX_COLOR_TRAILING_WHITESPACE].bg = COLOR_RED;
          syntax_defs[CE_SYNTAX_COLOR_VISUAL].fg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_VISUAL].bg = COLOR_WHITE;
          syntax_defs[CE_SYNTAX_COLOR_MATCH].fg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_MATCH].bg = COLOR_WHITE;
          syntax_defs[CE_SYNTAX_COLOR_CURRENT_LINE].fg = CE_SYNTAX_USE_CURRENT_COLOR;
          syntax_defs[CE_SYNTAX_COLOR_CURRENT_LINE].bg = COLOR_BRIGHT_BLACK;

          app.syntax_defs = syntax_defs;
     }

     // init config options
     {
          app.config_options.tab_width = 5;
          app.config_options.horizontal_scroll_off = 10;
          app.config_options.vertical_scroll_off = 5;
          app.config_options.insert_spaces_on_tab = true;
     }

     // init commands
     CeCommandEntry_t command_entries[] = {
          {command_quit, "quit", "quit ce"},
          {command_select_adjacent_layout, "select_adjacent_layout", "select 'left', 'right', 'up' or 'down adjacent layouts"},
          {command_save_buffer, "save_buffer", "save the currently selected view's buffer"},
          {command_show_buffers, "show_buffers", "show the list of buffers"},
          {command_show_yanks, "show_yanks", "show the state of your vim yanks"},
          {command_split_layout, "split_layout", "split the current layout 'horizontal' or 'vertical' into 2 layouts"},
          {command_select_parent_layout, "select_parent_layout", "select the parent of the current layout"},
          {command_delete_layout, "delete_layout", "delete the current layout (unless it's the only one left)"},
          {command_load_file, "load_file", "load a file (optionally specified)"},
          {command_new_tab, "new_tab", "create a new tab"},
          {command_select_adjacent_tab, "select_adjacent_tab", "selects either the 'left' or 'right' tab"},
          {command_search, "search", "interactive search 'forward' or 'backward'"},
          {command_regex_search, "regex_search", "interactive regex search 'forward' or 'backward'"},
          {command_command, "command", "interactively send a commmand"},
          {command_redraw, "redraw", "redraw the entire editor"},
          {command_switch_to_terminal, "switch_to_terminal", "if the terminal is in view, goto it, otherwise, open the terminal in the current view"},
          {command_switch_buffer, "switch_buffer", "open dialogue to switch buffer by name"},
          {command_goto_destination_in_line, "goto_destination_in_line", "scan current line for destination formats"},
          {command_replace_all, "replace_all", "replace all occurances below cursor (or within a visual range)"},
     };
     app.command_entries = command_entries;
     app.command_entry_count = sizeof(command_entries) / sizeof(command_entries[0]);

     app.buffer_list_buffer = calloc(1, sizeof(*app.buffer_list_buffer));
     app.yank_list_buffer = calloc(1, sizeof(*app.yank_list_buffer));
     app.complete_list_buffer = calloc(1, sizeof(*app.complete_list_buffer));
     app.macro_list_buffer = calloc(1, sizeof(*app.macro_list_buffer));

     // init buffers
     {
          ce_buffer_alloc(app.buffer_list_buffer, 1, "[buffers]");
          buffer_node_insert(&app.buffer_node_head, app.buffer_list_buffer);
          ce_buffer_alloc(app.yank_list_buffer, 1, "[yanks]");
          buffer_node_insert(&app.buffer_node_head, app.yank_list_buffer);
          ce_buffer_alloc(app.complete_list_buffer, 1, "[completions]");
          buffer_node_insert(&app.buffer_node_head, app.complete_list_buffer);
          ce_buffer_alloc(app.macro_list_buffer, 1, "[macros]");
          buffer_node_insert(&app.buffer_node_head, app.macro_list_buffer);

          app.buffer_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.yank_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.complete_list_buffer->status = CE_BUFFER_STATUS_NONE;
          app.macro_list_buffer->status = CE_BUFFER_STATUS_NONE;

          if(argc > 1){
               for(int64_t i = 1; i < argc; i++){
                    CeBuffer_t* buffer = calloc(1, sizeof(*buffer));
                    if(ce_buffer_load_file(buffer, argv[i])){
                         buffer_node_insert(&app.buffer_node_head, buffer);

                         // TODO: figure out type based on extention
                         buffer->type = CE_BUFFER_FILE_TYPE_C;
                    }else{
                         free(buffer);
                    }
               }
          }else{
               CeBuffer_t* buffer = calloc(1, sizeof(*buffer));
               ce_buffer_alloc(buffer, 1, "unnamed");
          }
     }

     // init keybinds
     {
          KeyBindDef_t normal_mode_bind_defs[] = {
               {{'\\', 'q'}, "quit"},
               {{8}, "select_adjacent_layout left"}, // ctrl h
               {{12}, "select_adjacent_layout right"}, // ctrl l
               {{11}, "select_adjacent_layout up"}, // ctrl k
               {{10}, "select_adjacent_layout down"}, // ctrl j
               {{23}, "save_buffer"}, // ctrl w
               {{'g', 'b'}, "show_buffers"},
               {{22}, "split_layout horizontal"}, // ctrl s
               {{19}, "split_layout vertical"}, // ctrl v
               {{16}, "select_parent_layout"}, // ctrl p
               {{KEY_CLOSE}, "delete_layout"}, // ctrl q
               {{6}, "load_file"}, // ctrl f
               {{20}, "new_tab"}, // ctrl t
               {{'/'}, "search forward"},
               {{'?'}, "search backward"},
               {{':'}, "command"},
               {{'g', 't'}, "select_adjacent_tab right"},
               {{'g', 'T'}, "select_adjacent_tab left"},
               {{'\\', '/'}, "regex_search forward"},
               {{'\\', '?'}, "regex_search backward"},
               {{'"', '?'}, "show_yanks"},
               {{'\\', 'r'}, "redraw"},
               {{24}, "switch_to_terminal"}, // ctrl x
               {{2}, "switch_buffer"}, // ctrl b
               {{343}, "goto_destination_in_line"}, // return
          };

          convert_bind_defs(&app.key_binds[CE_VIM_MODE_NORMAL], normal_mode_bind_defs, sizeof(normal_mode_bind_defs) / sizeof(normal_mode_bind_defs[0]));
     }

     // init vim
     {
          ce_vim_init(&app.vim);

          // override 'S' key
          // TODO: make function for this
          for(int64_t i = 0; i < app.vim.key_bind_count; ++i){
               CeVimKeyBind_t* key_bind = app.vim.key_binds + i;
               if(key_bind->key == 'S'){
                    key_bind->function = &custom_vim_parse_verb_substitute;
                    break;
               }
          }
     }

     // init layout
     {
          CeLayout_t* tab_layout = ce_layout_tab_init(app.buffer_node_head->buffer);
          app.tab_list_layout = ce_layout_tab_list_init(tab_layout);
          app_update_terminal_view(&app);
     }

     // init command complete
     {
          const char** commands = malloc(app.command_entry_count * sizeof(*commands));
          for(int64_t i = 0; i < app.command_entry_count; i++){
               commands[i] = strdup(app.command_entries[i].name);
          }
          ce_complete_init(&app.command_complete, commands, app.command_entry_count);
          for(int64_t i = 0; i < app.command_entry_count; i++){
               free((char*)(commands[i]));
          }
          free(commands);
     }

     // setup input buffer
     {
          CeBuffer_t* buffer = calloc(1, sizeof(*buffer));
          ce_buffer_alloc(buffer, 1, "input");
          app.input_view.buffer = buffer;
          buffer_node_insert(&app.buffer_node_head, buffer);
     }

     // init draw thread
     pthread_t thread_draw;
     {
          pthread_create(&thread_draw, NULL, draw_thread, &app);
          app.ready_to_draw = true;
     }

     // init terminal
     {
          getmaxyx(stdscr, app.terminal_height, app.terminal_width);
          ce_terminal_init(&app.terminal, app.terminal_width, app.terminal_height - 1);
          buffer_node_insert(&app.buffer_node_head, app.terminal.buffer);
     }

     // main loop
     while(!app.quit){
          // TODO: we can optimize by only doing this at the start and when we see a resized event
          app_update_terminal_view(&app);

          // figure out our current view rect
          CeView_t* view = NULL;
          CeLayout_t* tab_layout = app.tab_list_layout->tab_list.current;

          switch(tab_layout->tab.current->type){
          default:
               break;
          case CE_LAYOUT_TYPE_VIEW:
               view = &tab_layout->tab.current->view;
               break;
          }

          // setup input view rect
          if(view && app.input_mode) input_view_overlay(&app.input_view, view);

          int key = getch();
          app_handle_key(&app, view, key);

          // wait for input from the user
          if(ce_layout_buffer_in_view(tab_layout, app.buffer_list_buffer)){
               build_buffer_list(app.buffer_list_buffer, app.buffer_node_head);
          }

          if(ce_layout_buffer_in_view(tab_layout, app.yank_list_buffer)){
               build_yank_list(app.yank_list_buffer, app.vim.yanks);
          }

          if(ce_layout_buffer_in_view(tab_layout, app.macro_list_buffer)){
               build_macro_list(app.macro_list_buffer, &app.macros);
          }

          app.ready_to_draw = true;
     }

     // cleanup
     pthread_cancel(thread_draw);
     pthread_join(thread_draw, NULL);

     ce_terminal_free(&app.terminal);
     ce_macros_free(&app.macros);

     KeyBinds_t* binds = &app.key_binds[CE_VIM_MODE_NORMAL];
     for(int64_t i = 0; i < binds->count; ++i){
          ce_command_free(&binds->binds[i].command);
          if(!binds->binds[i].key_count) continue;
          free(binds->binds[i].keys);
     }
     free(binds->binds);

     buffer_node_free(&app.buffer_node_head);
     ce_layout_free(&app.tab_list_layout);
     ce_vim_free(&app.vim);
     endwin();
     return 0;
}
