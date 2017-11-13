#include "test.h"
#include "ce.h"

#include <stdlib.h>
#include <string.h>
#include <locale.h>

FILE* g_ce_log = NULL;
CeBuffer_t* g_ce_log_buffer = NULL;

const char* g_multiline_string = "0123456789\nabcdefghij\nklmnopqrst";
const char* g_multiline_string_with_empty_line = "0123456789\n\nabcdefghij\nklmnopqrst";
const char* g_name = "test.txt";

TEST(buffer_alloc_and_free){
     int line_count = 10;

     CeBuffer_t buffer = {};
     EXPECT(ce_buffer_alloc(&buffer, line_count, g_name));

     EXPECT(buffer.line_count == line_count);
     EXPECT(buffer.lines);
     EXPECT(strcmp(buffer.name, g_name) == 0);

     ce_buffer_free(&buffer);

     EXPECT(buffer.lines == NULL);
     EXPECT(buffer.line_count == 0);
     EXPECT(buffer.name == NULL);
}

TEST(buffer_load_string){
     CeBuffer_t buffer = {};
     EXPECT(ce_buffer_load_string(&buffer, g_multiline_string, g_name));

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "0123456789") == 0);
     EXPECT(strcmp(buffer.lines[1], "abcdefghij") == 0);
     EXPECT(strcmp(buffer.lines[2], "klmnopqrst") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_load_file){
     const char* filename = "test.txt";
     CeBuffer_t buffer = {};
     EXPECT(ce_buffer_load_file(&buffer, filename));

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 4);
     EXPECT(strcmp(buffer.lines[0], "this is just") == 0);
     EXPECT(strcmp(buffer.lines[1], "a file used") == 0);
     EXPECT(strcmp(buffer.lines[2], "for unittesting") == 0);
     EXPECT(strcmp(buffer.lines[3], "isn't that neato?") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_empty){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);

     ce_buffer_empty(&buffer);
     EXPECT(buffer.lines != NULL);
     EXPECT(buffer.line_count == 1);

     ce_buffer_free(&buffer);
}

TEST(buffer_range_len_single_line){
     CeBuffer_t buffer = {};
     EXPECT(ce_buffer_load_string(&buffer, "the only line", g_name));

     EXPECT(ce_buffer_range_len(&buffer, (CePoint_t){1, 0}, (CePoint_t){8, 0}) == 8);
}

TEST(buffer_range_len_two_lines){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);

     EXPECT(ce_buffer_range_len(&buffer, (CePoint_t){3, 0}, (CePoint_t){5, 1}) == 14);
}

TEST(buffer_range_len_three_lines){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);

     EXPECT(ce_buffer_range_len(&buffer, (CePoint_t){3, 0}, (CePoint_t){5, 2}) == 25);
}

TEST(buffer_line_len){
     CeBuffer_t buffer = {};
     EXPECT(ce_buffer_load_string(&buffer, g_multiline_string, g_name));

     EXPECT(ce_buffer_line_len(&buffer, 0) == 10);
     EXPECT(ce_buffer_line_len(&buffer, 1) == 10);
     EXPECT(ce_buffer_line_len(&buffer, 2) == 10);
     EXPECT(ce_buffer_line_len(&buffer, 3) == -1);

     ce_buffer_free(&buffer);
}

TEST(buffer_move_point){
     CePoint_t res;
     CeBuffer_t buffer = {};
     EXPECT(ce_buffer_load_string(&buffer, g_multiline_string, g_name));

     res = ce_buffer_move_point(&buffer, (CePoint_t){0, 0}, (CePoint_t){1, 0}, 1, CE_CLAMP_X_ON);
     EXPECT(res.x == 1 && res.y == 0);
     res = ce_buffer_move_point(&buffer, (CePoint_t){0, 0}, (CePoint_t){0, 1}, 1, CE_CLAMP_X_ON);
     EXPECT(res.x == 0 && res.y == 1);

     // too far, clamp
     res = ce_buffer_move_point(&buffer, (CePoint_t){0, 0}, (CePoint_t){0, 5}, 1, CE_CLAMP_X_ON);
     EXPECT(res.x == 0 && res.y == 2);
     res = ce_buffer_move_point(&buffer, (CePoint_t){0, 0}, (CePoint_t){100, 0}, 1, CE_CLAMP_X_ON);
     EXPECT(res.x == 10 && res.y == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_contains_point){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);

     EXPECT(ce_buffer_contains_point(&buffer, (CePoint_t){0, 0}));
     EXPECT(ce_buffer_contains_point(&buffer, (CePoint_t){9, 0}));
     EXPECT(!ce_buffer_contains_point(&buffer, (CePoint_t){11, 0}));
     EXPECT(ce_buffer_contains_point(&buffer, (CePoint_t){5, 2}));
     EXPECT(!ce_buffer_contains_point(&buffer, (CePoint_t){0, 3}));

     const char* utf8_string = "$¢€𐍈";
     ce_buffer_load_string(&buffer, utf8_string, g_name);

     EXPECT(ce_buffer_contains_point(&buffer, (CePoint_t){0, 0}));
     EXPECT(ce_buffer_contains_point(&buffer, (CePoint_t){3, 0}));
     EXPECT(!ce_buffer_contains_point(&buffer, (CePoint_t){4, 0}));

     ce_buffer_free(&buffer);
}

TEST(buffer_insert_string_one_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     const char* string = "taco";
     ce_buffer_insert_string(&buffer, string, (CePoint_t){2, 1});

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "0123456789") == 0);
     EXPECT(strcmp(buffer.lines[1], "abtacocdefghij") == 0);
     EXPECT(strcmp(buffer.lines[2], "klmnopqrst") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_insert_string_two_lines){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     const char* string = "taco\ncat";
     ce_buffer_insert_string(&buffer, string, (CePoint_t){6, 0});

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 4);
     EXPECT(strcmp(buffer.lines[0], "012345taco") == 0);
     EXPECT(strcmp(buffer.lines[1], "cat6789") == 0);
     EXPECT(strcmp(buffer.lines[2], "abcdefghij") == 0);
     EXPECT(strcmp(buffer.lines[3], "klmnopqrst") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_insert_string_three_lines){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     const char* string = "taco\ncat\npizza";
     ce_buffer_insert_string(&buffer, string, (CePoint_t){7, 1});

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 5);
     EXPECT(strcmp(buffer.lines[0], "0123456789") == 0);
     EXPECT(strcmp(buffer.lines[1], "abcdefgtaco") == 0);
     EXPECT(strcmp(buffer.lines[2], "cat") == 0);
     EXPECT(strcmp(buffer.lines[3], "pizzahij") == 0);
     EXPECT(strcmp(buffer.lines[4], "klmnopqrst") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_insert_string_newline){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     const char* string = "\n";
     ce_buffer_insert_string(&buffer, string, (CePoint_t){7, 1});

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 4);
     EXPECT(strcmp(buffer.lines[0], "0123456789") == 0);
     EXPECT(strcmp(buffer.lines[1], "abcdefg") == 0);
     EXPECT(strcmp(buffer.lines[2], "hij") == 0);
     EXPECT(strcmp(buffer.lines[3], "klmnopqrst") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_insert_string_newline_at_end_of_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     const char* string = "\n";
     ce_buffer_insert_string(&buffer, string, (CePoint_t){10, 0});

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 4);
     EXPECT(strcmp(buffer.lines[0], "0123456789") == 0);
     EXPECT(strcmp(buffer.lines[1], "") == 0);
     EXPECT(strcmp(buffer.lines[2], "abcdefghij") == 0);
     EXPECT(strcmp(buffer.lines[3], "klmnopqrst") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_insert_string_on_empty_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, "first line\n\nthird line", g_name);
     const char* string = "inserted";
     ce_buffer_insert_string(&buffer, string, (CePoint_t){0, 1});

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "first line") == 0);
     EXPECT(strcmp(buffer.lines[1], "inserted") == 0);
     EXPECT(strcmp(buffer.lines[2], "third line") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_remove_string_portion_of_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){3, 0}, 5));
     EXPECT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "01289") == 0);
}

TEST(buffer_remove_string_entire_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){0, 0}, 10));
     EXPECT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "") == 0);
}

TEST(buffer_remove_string_entire_line_plus_newline){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){0, 0}, 11));
     EXPECT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "abcdefghij") == 0);
}

TEST(buffer_remove_string_entire_line_multiple){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){0, 0}, 21));
     EXPECT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "") == 0);
}

TEST(buffer_remove_string_entire_line_multiple_with_newline){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){0, 0}, 22));
     EXPECT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "klmnopqrst") == 0);
}

TEST(buffer_remove_string_across_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){5, 0}, 10));
     EXPECT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "01234efghij") == 0);
}

TEST(buffer_remove_string_join){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){10, 0}, 1));
     EXPECT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "0123456789abcdefghij") == 0);
}

TEST(buffer_remove_string_up_to_join){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){8, 0}, 3));
     EXPECT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "01234567abcdefghij") == 0);
}

TEST(buffer_remove_string_join_plus){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){10, 0}, 5));
     EXPECT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "0123456789efghij") == 0);
}

TEST(buffer_remove_string_join_minus){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){8, 0}, 5));
     EXPECT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "01234567cdefghij") == 0);
}

TEST(buffer_remove_string_empty_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string_with_empty_line, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){0, 1}, 1));
     EXPECT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "0123456789") == 0);
     EXPECT(strcmp(buffer.lines[1], "abcdefghij") == 0);
}

TEST(buffer_remove_string_empty_line_plus){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string_with_empty_line, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){0, 1}, 3));
     EXPECT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "0123456789") == 0);
     EXPECT(strcmp(buffer.lines[1], "cdefghij") == 0);
}

TEST(buffer_remove_string_empty_line_minus){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string_with_empty_line, g_name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){8, 0}, 5));
     EXPECT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "01234567bcdefghij") == 0);
}

TEST(buffer_remove_string_last_empty_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     ce_buffer_insert_string(&buffer, "\n", (CePoint_t){10, 2});
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){10, 2}, 1));
     EXPECT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "0123456789") == 0);
     EXPECT(strcmp(buffer.lines[1], "abcdefghij") == 0);
     EXPECT(strcmp(buffer.lines[2], "klmnopqrst") == 0);
}

TEST(buffer_dupe_string_portion_of_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){3, 0}, 5);
     EXPECT(strcmp(dupe, "34567") == 0);
}

TEST(buffer_dupe_string_entire_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){0, 0}, 10);
     EXPECT(strcmp(dupe, "0123456789") == 0);
}

TEST(buffer_dupe_string_entire_line_plus_newline){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){0, 0}, 11);
     EXPECT(strcmp(dupe, "0123456789\n") == 0);
}

TEST(buffer_dupe_string_entire_line_multiple){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){0, 0}, 21);
     EXPECT(strcmp(dupe, "0123456789\nabcdefghij") == 0);
}

TEST(buffer_dupe_string_entire_line_multiple_plus_newline){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){0, 0}, 22);
     EXPECT(strcmp(dupe, "0123456789\nabcdefghij\n") == 0);
}

TEST(buffer_dupe_string_across_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){5, 0}, 10);
     EXPECT(strcmp(dupe, "56789\nabcd") == 0);
}

TEST(buffer_dupe_string_across_multiplie_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){5, 0}, 20);
     EXPECT(strcmp(dupe, "56789\nabcdefghij\nklm") == 0);
}

TEST(buffer_dupe_string_blank_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string_with_empty_line, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){0, 1}, 1);
     EXPECT(strcmp(dupe, "\n") == 0);
}

TEST(buffer_dupe_string_blank_line_plus){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string_with_empty_line, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){0, 1}, 5);
     EXPECT(strcmp(dupe, "\nabcd") == 0);
}

TEST(buffer_dupe_string_blank_line_included){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string_with_empty_line, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){5, 0}, 7);
     EXPECT(strcmp(dupe, "56789\n\n") == 0);
}

TEST(buffer_dupe_string_blank_line_in_middle){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string_with_empty_line, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){5, 0}, 10);
     EXPECT(strcmp(dupe, "56789\n\nabc") == 0);
}

TEST(buffer_dupe_string_entire_buffer){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string_with_empty_line, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){0, 0}, 33);
     EXPECT(strcmp(dupe, "0123456789\n\nabcdefghij\nklmnopqrst") == 0);
}

TEST(buffer_dupe_string_entire_buffer_plus_newline){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string_with_empty_line, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){0, 0}, 34);
     EXPECT(strcmp(dupe, "0123456789\n\nabcdefghij\nklmnopqrst\n") == 0);
}

TEST(buffer_dupe_string_outside){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string_with_empty_line, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){5, 5}, 10);
     EXPECT(dupe == NULL);
}

TEST(buffer_dupe_string_too_long){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string_with_empty_line, g_name);
     char* dupe = ce_buffer_dupe_string(&buffer, (CePoint_t){0, 0}, 50);
     EXPECT(dupe == NULL);
}

TEST(view_follow_cursor){
     int64_t tab_width = 2;
     int64_t horizontal_scroll_off = 2;
     int64_t vertical_scroll_off = 3;

     const char* string = "int main(int argc, char** argv){\n" \
                          "\tif(argc != 3){\n"                 \
                          "\t\tprint_help(argv[0]);\n"         \
                          "\t}\n"                              \
                          "\treturn 0;\n"                      \
                          "}";

     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, string, "test.c");

     CeView_t view = {};
     view.rect.right = 5;
     view.rect.bottom = 4;
     view.buffer = &buffer;

     // do nothing
     ce_view_follow_cursor(&view, horizontal_scroll_off, vertical_scroll_off, tab_width);
     EXPECT(view.scroll.x == 0);
     EXPECT(view.scroll.y == 0);

     // scroll right
     view.cursor.x = 6;
     ce_view_follow_cursor(&view, horizontal_scroll_off, vertical_scroll_off, tab_width);
     EXPECT(view.scroll.x == 3);
     EXPECT(view.scroll.y == 0);

     // scroll down, accounting for tabs
     view.cursor.y = 2;
     ce_view_follow_cursor(&view, horizontal_scroll_off, vertical_scroll_off, tab_width);
     EXPECT(view.scroll.x == 5);
     EXPECT(view.scroll.y == 0);

     // scroll down again, accounting for tabs
     view.cursor.x = 1;
     view.cursor.y = 3;
     ce_view_follow_cursor(&view, horizontal_scroll_off, vertical_scroll_off, tab_width);
     EXPECT(view.scroll.x == 0);
     EXPECT(view.scroll.y == 2);

     // scroll back to origin
     view.cursor.x = 0;
     view.cursor.y = 0;
     ce_view_follow_cursor(&view, horizontal_scroll_off, vertical_scroll_off, tab_width);
     EXPECT(view.scroll.x == 0);
     EXPECT(view.scroll.y == 0);

     ce_buffer_free(&buffer);
}

TEST(utf8_strlen){
     const char* ascii_only = "tacos";
     const char* mix = "ta¢𐍈s";
     const char* ut8_only = "¢€𐍈";

     EXPECT(ce_utf8_strlen(ascii_only) == 5);
     EXPECT(ce_utf8_strlen(mix) == 5);
     EXPECT(ce_utf8_strlen(ut8_only) == 3);
}

#if 0
TEST(utf8_find_index){

}

TEST(utf8_encode){
     char utf8[CE_UTF8_SIZE + 1];
     int64_t written = 0;
     EXPECT(ce_utf8_encode(0x20AC, utf8, CE_UTF8_SIZE, &written));
     EXPECT(written == 3);
     EXPECT(utf8[0] == (char)(0x93));
     EXPECT(utf8[1] == (char)(0xab));
     EXPECT(utf8[2] == (char)(0x3f));
}
#endif

TEST(util_count_string_lines){
     const char* one_line = "pot belly's";
     const char* two_lines = "meatball subs are\nmediocre";
     const char* three_lines = "is that\na mean thing\nto say?";

     EXPECT(ce_util_count_string_lines(one_line) == 1);
     EXPECT(ce_util_count_string_lines(two_lines) == 2);
     EXPECT(ce_util_count_string_lines(three_lines) == 3);
}

TEST(util_string_index_to_visible_index){
     int64_t tab_width = 8;
     const char* normal_string = "hello world";
     const char* tabbed_string = "\t\tgoodbye\t world";

     EXPECT(ce_util_string_index_to_visible_index(normal_string, 4, tab_width) == 4);
     EXPECT(ce_util_string_index_to_visible_index(normal_string, 8, tab_width) == 8);

     EXPECT(ce_util_string_index_to_visible_index(tabbed_string, 4, tab_width) == 18);
     EXPECT(ce_util_string_index_to_visible_index(tabbed_string, 8, tab_width) == 22);
     EXPECT(ce_util_string_index_to_visible_index(tabbed_string, 12, tab_width) == 33);
}

TEST(util_visible_index_to_string_index){
     int64_t tab_width = 8;
     const char* normal_string = "hello world";
     const char* tabbed_string = "\t\tgoodbye\t world";

     EXPECT(ce_util_visible_index_to_string_index(normal_string, 4, tab_width) == 4);
     EXPECT(ce_util_visible_index_to_string_index(normal_string, 8, tab_width) == 8);

     EXPECT(ce_util_visible_index_to_string_index(tabbed_string, 18, tab_width) == 4);
     EXPECT(ce_util_visible_index_to_string_index(tabbed_string, 22, tab_width) == 8);
     EXPECT(ce_util_visible_index_to_string_index(tabbed_string, 33, tab_width) == 12);
}

int main()
{
     g_ce_log_buffer = malloc(sizeof(*g_ce_log_buffer));
     ce_buffer_alloc(g_ce_log_buffer, 1, "[log]");
     ce_log_init("ce_test.log");
     setlocale(LC_ALL, "");
     RUN_TESTS();
}
