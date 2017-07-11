#include "test.h"
#include "ce.h"

#include <string.h>
#include <locale.h>

const char* g_multiline_string = "first line\nsecond line\nthird line";
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
     EXPECT(strcmp(buffer.lines[0], "first line") == 0);
     EXPECT(strcmp(buffer.lines[1], "second line") == 0);
     EXPECT(strcmp(buffer.lines[2], "third line") == 0);

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
     const char* string = "simple";
     ce_buffer_insert_string(&buffer, string, (CePoint_t){2, 1});

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "first line") == 0);
     EXPECT(strcmp(buffer.lines[1], "sesimplecond line") == 0);
     EXPECT(strcmp(buffer.lines[2], "third line") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_insert_string_two_lines){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     const char* string = "inserted first line\ninserted second line";
     ce_buffer_insert_string(&buffer, string, (CePoint_t){6, 0});

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 4);
     EXPECT(strcmp(buffer.lines[0], "first inserted first line") == 0);
     EXPECT(strcmp(buffer.lines[1], "inserted second lineline") == 0);
     EXPECT(strcmp(buffer.lines[2], "second line") == 0);
     EXPECT(strcmp(buffer.lines[3], "third line") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_insert_string_three_lines){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     const char* string = "one\ntwo\nthree";
     ce_buffer_insert_string(&buffer, string, (CePoint_t){7, 1});

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 5);
     EXPECT(strcmp(buffer.lines[0], "first line") == 0);
     EXPECT(strcmp(buffer.lines[1], "second one") == 0);
     EXPECT(strcmp(buffer.lines[2], "two") == 0);
     EXPECT(strcmp(buffer.lines[3], "threeline") == 0);
     EXPECT(strcmp(buffer.lines[4], "third line") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_remove_string_partial_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);

     ce_buffer_remove_string(&buffer, (CePoint_t){2, 1}, 4, false);
     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "first line") == 0);
     EXPECT(strcmp(buffer.lines[1], "se line") == 0);
     EXPECT(strcmp(buffer.lines[2], "third line") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_remove_string_entire_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     ce_buffer_remove_string(&buffer, (CePoint_t){0, 1}, 11, false);

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "first line") == 0);
     EXPECT(strcmp(buffer.lines[1], "") == 0);
     EXPECT(strcmp(buffer.lines[2], "third line") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_remove_string_entire_line_and_remove_line){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     ce_buffer_remove_string(&buffer, (CePoint_t){0, 1}, 11, true);

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "first line") == 0);
     EXPECT(strcmp(buffer.lines[1], "third line") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_remove_lines_single){
     CeBuffer_t buffer = {};
     EXPECT(!ce_buffer_remove_lines(&buffer, 0, 1));
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     EXPECT(ce_buffer_remove_lines(&buffer, 1, 1));

     EXPECT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "first line") == 0);
     EXPECT(strcmp(buffer.lines[1], "third line") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_remove_lines_multiple){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);
     EXPECT(ce_buffer_remove_lines(&buffer, 0, 2));

     EXPECT(buffer.line_count == 1);
     EXPECT(strcmp(buffer.lines[0], "third line") == 0);

     ce_buffer_free(&buffer);
}

TEST(buffer_remove_lines_invalid){
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, g_multiline_string, g_name);

     EXPECT(!ce_buffer_remove_lines(&buffer, 0, 4));
     EXPECT(!ce_buffer_remove_lines(&buffer, 0, 0));
     EXPECT(!ce_buffer_remove_lines(&buffer, -1, 1));
     EXPECT(!ce_buffer_remove_lines(&buffer, 0, -1));

     ce_buffer_free(&buffer);
}

TEST(buffer_remove_string_four_lines){
     CeBuffer_t buffer = {};
     const char* name = "test.txt";
     ce_buffer_load_string(&buffer, "0123456789\n0123456789\n0123456789\n0123456789\n0123456789", name);
     EXPECT(ce_buffer_remove_string(&buffer, (CePoint_t){5, 1}, 27, false));

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 2);
     EXPECT(strcmp(buffer.lines[0], "0123456789") == 0);
     EXPECT(strcmp(buffer.lines[1], "0123423456789") == 0);

     ce_buffer_free(&buffer);
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
     ce_log_init("ce_test.log");
     setlocale(LC_ALL, "");
     RUN_TESTS();
}
