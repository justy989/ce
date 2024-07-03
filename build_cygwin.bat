@echo off
mkdir build\windows
pushd build\windows
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.40.33807\bin\Hostx64\x64\cl" ^
  /D_AMD64_ /DDISPLAY_GUI /DPLATFORM_WINDOWS ^
  /I "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um" ^
  /I "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared" ^
  /I "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\ucrt" ^
  /I "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.40.33807\include" ^
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
  ..\..\ce_subprocess.c ^
  ..\..\ce_syntax.c ^
  ..\..\ce_vim.c ^
  "SDL2.lib" "SDL2_ttf.lib" "SDL2main.lib" "shell32.lib" ^
  /link ^
  "/LIBPATH:C:\Program Files (x86)\Windows Kits\10\lib\10.0.22621.0\um\x64" ^
  "/LIBPATH:C:\Program Files (x86)\Windows Kits\10\lib\10.0.22621.0\ucrt\x64" ^
  "/LIBPATH:C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.40.33807\lib\x64" ^
  "/LIBPATH:..\..\external\lib" ^
  "/LIBPATH:..\..\external\lib" ^
  "/SUBSYSTEM:CONSOLE" ^
  "/out:ce_gui.exe"
popd
