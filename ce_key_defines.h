#pragma once

#if defined(DISPLAY_TERMINAL)
  #include <ncurses.h>

  #define KEY_UP_ARROW KEY_UP
  #define KEY_DOWN_ARROW KEY_DOWN
  #define KEY_LEFT_ARROW KEY_LEFT
  #define KEY_RIGHT_ARROW KEY_RIGHT
  #define KEY_RESIZE_EVENT KEY_RESIZE
  #define KEY_CARRIAGE_RETURN KEY_ENTER
  #define KEY_ONLY_BACKSPACE KEY_BACKSPACE
  #define KEY_DELETE KEY_DC
  #define KEY_INVALID ERR
  #define KEY_CTRL_F 6
#elif defined(DISPLAY_GUI)
  // TODO: figure out the real values here.
  #define KEY_UP_ARROW -2
  #define KEY_DOWN_ARROW -3
  #define KEY_LEFT_ARROW -4
  #define KEY_RIGHT_ARROW -5
  #define KEY_RESIZE_EVENT -6
  #define KEY_PPAGE -8
  #define KEY_NPAGE -9
  #define KEY_HOME -10
  #define KEY_CLOSE -11
  #define KEY_INVALID -12

  #define KEY_ONLY_BACKSPACE 8
  #define KEY_CARRIAGE_RETURN 13
  #define KEY_REDO 18
  #define KEY_DELETE 127
  #define KEY_CTRL_SHIFT_C -29
  #define KEY_CTRL_SHIFT_V -10
  #define KEY_CTRL_F 102
#endif

#define KEY_ESCAPE 27
