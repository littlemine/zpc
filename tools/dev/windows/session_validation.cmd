@echo off
setlocal

call "%~dp0_vsdevcmd_x64.cmd"
if errorlevel 1 exit /b %errorlevel%

for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
set "BUILD_DIR=%ROOT%\build-session-validation"

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja -DZS_ENABLE_TEST=ON -DZS_ENABLE_CUDA=OFF -DZS_ENABLE_VULKAN=OFF -DZS_ENABLE_JIT=OFF
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --target validationschema validationformat validationcompare validationpersistence audiofoundation audiovalidation interfaceservices
if errorlevel 1 exit /b %errorlevel%

ctest --test-dir "%BUILD_DIR%" -R "ZsValidationSchema|ZsValidationFormat|ZsValidationCompare|ZsValidationPersistence|ZsAudioFoundation|ZsAudioValidation|ZsInterfaceServices" --output-on-failure
exit /b %errorlevel%
