#include "test.h"
#include "ce.h"

#include <string.h>
#include <locale.h>

TEST(buffer_alloc_and_free)
{
     int line_count = 10;
     const char* name = "test.txt";

     CeBuffer_t buffer = {};
     EXPECT(ce_buffer_alloc(&buffer, line_count, name));

     EXPECT(buffer.line_count == line_count);
     EXPECT(buffer.lines);
     EXPECT(strcmp(buffer.name, name) == 0);

     ce_buffer_free(&buffer);

     EXPECT(buffer.lines == NULL);
     EXPECT(buffer.line_count == 0);
     EXPECT(buffer.name == NULL);
}

TEST(buffer_load_string)
{
     const char* name = "test.txt";
     const char* string = "hello, this is\na plain text\nstring of doom.";
     CeBuffer_t buffer = {};
     EXPECT(ce_buffer_load_string(&buffer, string, name));

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 3);
     EXPECT(strcmp(buffer.lines[0], "hello, this is") == 0);
     EXPECT(strcmp(buffer.lines[1], "a plain text") == 0);
     EXPECT(strcmp(buffer.lines[2], "string of doom.") == 0);
}

TEST(buffer_load_file)
{
     const char* filename = "test.txt";
     CeBuffer_t buffer = {};
     EXPECT(ce_buffer_load_file(&buffer, filename));

     EXPECT(buffer.lines);
     EXPECT(buffer.line_count == 4);
     EXPECT(strcmp(buffer.lines[0], "this is just") == 0);
     EXPECT(strcmp(buffer.lines[1], "a file used") == 0);
     EXPECT(strcmp(buffer.lines[2], "for unittesting") == 0);
     EXPECT(strcmp(buffer.lines[3], "isn't that neato?") == 0);
}

TEST(buffer_empty)
{
     const char* name = "test.txt";
     const char* string = "hello, this is\na plain text\nstring of doom.";
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, string, name);

     ce_buffer_empty(&buffer);
     EXPECT(buffer.lines != NULL);
     EXPECT(buffer.line_count == 1);
}

TEST(buffer_contains_point)
{
     const char* name = "test.txt";
     const char* string = "hello, this is\na plain text\nstring of doom.";
     CeBuffer_t buffer = {};
     ce_buffer_load_string(&buffer, string, name);

     EXPECT(ce_buffer_contains_point(&buffer, (CePoint_t){0, 0}));
     EXPECT(ce_buffer_contains_point(&buffer, (CePoint_t){13, 0}));
     EXPECT(!ce_buffer_contains_point(&buffer, (CePoint_t){14, 0}));
     EXPECT(ce_buffer_contains_point(&buffer, (CePoint_t){14, 2}));
     EXPECT(!ce_buffer_contains_point(&buffer, (CePoint_t){0, 3}));

     const char* utf8_string = "$¢€𐍈";
     ce_buffer_load_string(&buffer, utf8_string, name);

     EXPECT(ce_buffer_contains_point(&buffer, (CePoint_t){0, 0}));
     EXPECT(ce_buffer_contains_point(&buffer, (CePoint_t){3, 0}));
     EXPECT(!ce_buffer_contains_point(&buffer, (CePoint_t){4, 0}));
}

int main()
{
     setlocale(LC_ALL, "");
     RUN_TESTS();
}
