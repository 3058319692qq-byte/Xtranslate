@echo off
rem Static slim OpenCV (core + imgproc) build for XTranslate.
rem Runtime = dynamic CRT (/MD) to match Qt6 and the app (BUILD_WITH_STATIC_CRT=OFF).
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "SRC=e:\Transform\XTranslate\third_party\opencv"
set "BLD=e:\Transform\XTranslate\third_party\opencv-build"
set "INST=e:\Transform\XTranslate\third_party\opencv-install"

rem Clean previous generated trees for a reproducible slim build.
if exist "%BLD%"  rmdir /s /q "%BLD%"
if exist "%INST%" rmdir /s /q "%INST%"

"%CMAKE%" -S "%SRC%" -B "%BLD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
  -DBUILD_LIST=core,imgproc ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DBUILD_WITH_STATIC_CRT=OFF ^
  -DBUILD_opencv_world=OFF ^
  -DBUILD_opencv_imgcodecs=OFF ^
  -DWITH_FFMPEG=OFF -DWITH_MSMF=OFF -DWITH_DSHOW=OFF ^
  -DWITH_OPENCL=OFF -DWITH_IPP=OFF ^
  -DWITH_TIFF=OFF -DWITH_JPEG=OFF -DWITH_PNG=OFF -DWITH_WEBP=OFF ^
  -DWITH_OPENJPEG=OFF -DWITH_JASPER=OFF -DWITH_OPENEXR=OFF ^
  -DWITH_PROTOBUF=OFF -DWITH_QUIRC=OFF -DWITH_ADE=OFF ^
  -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF ^
  -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF ^
  -DBUILD_JAVA=OFF -DBUILD_opencv_python3=OFF ^
  -DCMAKE_INSTALL_PREFIX="%INST%" || exit /b 2

"%CMAKE%" --build "%BLD%" || exit /b 3
"%CMAKE%" --install "%BLD%" || exit /b 4

echo OPENCV_BUILD_OK
