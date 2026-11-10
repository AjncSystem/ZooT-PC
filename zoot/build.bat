@echo off
rem ZooT-PC build: eden-cli (no Qt) with clang-cl + ThinLTO + PGO, as the Lichii Windows CI does.
rem Everything stays inside the ZooT-PC folder (no subst drive); the folder path must have no
rem spaces, since the PGO profile path goes inside the compiler flags.
rem Usage: zoot\build.bat [configure] [nopgo]
setlocal EnableDelayedExpansion

set "ROOT=%~dp0..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
if not "%ROOT: =%"=="%ROOT%" (echo The folder path "%ROOT%" has spaces: rename it. & exit /b 1)
set "R=%ROOT:\=/%"

set "VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set "LLVM=I:/msys64/clang64/bin"
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%;I:\msys64\clang64\bin"

set "BUILD=%R%/build"
set "FLAGS=-DWIN32 -D_WINDOWS -DNOMINMAX -flto=thin"
if /i not "%~2"=="nopgo" if /i not "%~1"=="nopgo" (
  set "FLAGS=!FLAGS! -fprofile-use=%R%/src/lichii-windows.profdata -Wno-error=profile-instr-out-of-date -Wno-error=profile-instr-unprofiled -Wno-error=backend-plugin"
)

if /i "%~1"=="configure" goto configure
if not exist "%BUILD%/build.ninja" goto configure
goto build

:configure
"%CMAKE%" -S %R%/src -B %BUILD% -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBUILD_TESTING=OFF -DYUZU_TESTS=OFF ^
  -DENABLE_QT=OFF -DYUZU_CMD=ON ^
  -DYUZU_ROOM=OFF -DYUZU_ROOM_STANDALONE=OFF ^
  -DYUZU_USE_BUNDLED_SDL3=ON ^
  -DENABLE_UPDATE_CHECKER=OFF -DENABLE_WEB_SERVICE=OFF ^
  -DUSE_DISCORD_PRESENCE=OFF ^
  -DLICHII_BUILD_TAG=zoot-dev -DLICHII_BUILD_PLATFORM=windows-x86_64-zoot ^
  "-DCMAKE_C_COMPILER=%LLVM%/clang-cl.exe" ^
  "-DCMAKE_CXX_COMPILER=%LLVM%/clang-cl.exe" ^
  "-DCMAKE_LINKER=%LLVM%/lld-link.exe" ^
  "-DCMAKE_AR=%LLVM%/llvm-lib.exe" ^
  "-DCMAKE_RC_COMPILER=%LLVM%/llvm-rc.exe" ^
  "-DCMAKE_C_FLAGS=%FLAGS%" ^
  "-DCMAKE_CXX_FLAGS=-EHsc %FLAGS%" || exit /b 1

:build
"%CMAKE%" --build %BUILD% --target yuzu-cmd --parallel 20 || exit /b 1
echo ZooT-PC build OK
