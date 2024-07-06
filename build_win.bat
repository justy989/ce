@echo off
mkdir build\windows
pushd build\windows
cl ^
  /D_AMD64_ ^
  /DDISPLAY_GUI ^
  /DPLATFORM_WINDOWS ^
  /I "..\..\external\include" ^
  ..\..\main.c ^
  ..\..\ce.c ^
  ..\..\ce_app.c ^
  ..\..\ce_command.c ^
  ..\..\ce_commands.c ^
  ..\..\ce_complete.c ^
  ..\..\ce_draw_gui.c ^
  ..\..\ce_layout.c ^
  ..\..\ce_macros.c ^
  ..\..\ce_regex_windows.cpp ^
  ..\..\ce_subprocess.c ^
  ..\..\ce_syntax.c ^
  ..\..\ce_vim.c ^
  "SDL2.lib" "SDL2_ttf.lib" "SDL2main.lib" "shell32.lib" ^
  /link ^
  "/LIBPATH:..\..\external\lib" ^
  "/SUBSYSTEM:CONSOLE" ^
  "/out:ce_gui.exe"
popd

