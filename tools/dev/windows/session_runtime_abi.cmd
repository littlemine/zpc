@echo off
setlocal

call "%~dp0_vsdevcmd_x64.cmd"
if errorlevel 1 exit /b %errorlevel%

set "ROOT=H:\Codes\zpc"
set "BUILD_DIR=%ROOT%\build-session-runtime-abi"

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja -DZS_ENABLE_TEST=ON -DZS_ENABLE_CUDA=OFF -DZS_ENABLE_VULKAN=OFF -DZS_ENABLE_JIT=OFF
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --target asyncruntimeabi asyncruntimeabidemo
if errorlevel 1 exit /b %errorlevel%

ctest --test-dir "%BUILD_DIR%" -R ZsAsyncRuntimeAbi --output-on-failure
if errorlevel 1 exit /b %errorlevel%

echo [zpc] Running async runtime ABI demo artifact...
"%BUILD_DIR%\test\asyncruntimeabidemo.exe"
exit /b %errorlevel%