#pragma once

#include <stdio.h>

// We currently rely on __COUNTER__ being 0 to start!
// When we run tests, we use pointer arithmetic on global function pointers and this is "Undefined Behavior" according
// to the c standard

typedef void test_func_t(int*);

#define INDIR_TEST_FUNC(n) g_test_func_##n
#define GLOBAL_TEST_FUNC(n) INDIR_TEST_FUNC(n)

// define test functions
#define TEST(name)                                      \
     void name(int* _test_failed);                      \
     test_func_t* GLOBAL_TEST_FUNC(__COUNTER__) = name; \
     void name(int* _test_failed)

// pass conditionals that will cause the test to fail if they evaluate to false
#define EXPECT(cond)                                                                              \
     if(!(cond)){                                                                                 \
          printf("%s:%d: %s() FAILED expecting (%s)\n", __FILE__, __LINE__, __FUNCTION__, #cond); \
          *_test_failed = 1;                                                                      \
     }

// call this in main after any setup you need to do
#define RUN_TESTS()                                      \
     int tests_failed = 0;                               \
     int test_count = __COUNTER__;                       \
     printf("executing %d test(s)\n\n", test_count);     \
     for(int i = 0; i < test_count; ++i){                \
          int failed = 0;                                \
          (*(&g_test_func_0 + i))(&failed);              \
          if(failed) tests_failed++;                     \
     }                                                   \
     if(tests_failed){                                   \
          printf("\n%d test(s) failed\n", tests_failed); \
          return 1;                                      \
     }                                                   \
     printf("\nall test(s) passed\n");                   \
     return 0;
