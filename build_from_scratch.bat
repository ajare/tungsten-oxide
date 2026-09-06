@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "WITH_MPP_LFS=false"
set "BUILD_TYPE=Release"
set "BUILD_DIR=build-windows"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--with-mpp-lfs" (
    set "WITH_MPP_LFS=true"
    shift
    goto parse_args
)
if /i "%~1"=="--config" (
    if "%~2"=="" (
        set "ERROR_MESSAGE=--config requires a value"
        goto fatal
    )
    set "BUILD_TYPE=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="--build-dir" (
    if "%~2"=="" (
        set "ERROR_MESSAGE=--build-dir requires a value"
        goto fatal
    )
    set "BUILD_DIR=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-h" goto usage_success
if /i "%~1"=="--help" goto usage_success
set "ERROR_MESSAGE=unknown option: %~1 (run with --help for usage)"
goto fatal

:args_done
where git >nul 2>&1
if errorlevel 1 (
    set "ERROR_MESSAGE=Git is required but was not found on PATH"
    goto fatal
)
where cmake >nul 2>&1
if errorlevel 1 (
    set "ERROR_MESSAGE=CMake is required but was not found on PATH"
    goto fatal
)

for %%I in ("%~dp0.") do set "ROOT_DIR=%%~fI"
pushd "%ROOT_DIR%"
if errorlevel 1 (
    set "ERROR_MESSAGE=could not enter repository directory: %ROOT_DIR%"
    goto fatal
)
set "PUSHD_DONE=true"

git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
    set "ERROR_MESSAGE=%ROOT_DIR% is not a Git checkout"
    goto fatal
)
if not exist "CMakeLists.txt" (
    set "ERROR_MESSAGE=run this script from the TungstenOxide checkout"
    goto fatal
)
if not exist ".gitmodules" (
    set "ERROR_MESSAGE=run this script from the TungstenOxide checkout"
    goto fatal
)

if /i "%WITH_MPP_LFS%"=="true" (
    git lfs version >nul 2>&1
    if errorlevel 1 (
        set "ERROR_MESSAGE=--with-mpp-lfs requires Git LFS, but 'git lfs' is not installed"
        goto fatal
    )
)

echo Synchronizing and checking out all submodules...
git submodule sync --recursive
if errorlevel 1 (
    set "ERROR_MESSAGE=failed to synchronize submodules"
    goto fatal
)
git submodule update --init --recursive
if errorlevel 1 (
    set "ERROR_MESSAGE=failed to check out submodules"
    goto fatal
)

set "SUBMODULE_STATUS_FILE=%TEMP%\tungsten-oxide-submodules-%RANDOM%-%RANDOM%.txt"
git submodule status --recursive > "%SUBMODULE_STATUS_FILE%"
if errorlevel 1 (
    set "ERROR_MESSAGE=failed to inspect submodules"
    goto fatal
)
type "%SUBMODULE_STATUS_FILE%"
findstr /r /b /c:"[+U-]" "%SUBMODULE_STATUS_FILE%" >nul
if not errorlevel 1 (
    del /q "%SUBMODULE_STATUS_FILE%" >nul 2>&1
    set "ERROR_MESSAGE=one or more submodules are not checked out at the commits recorded by their parent"
    goto fatal
)
del /q "%SUBMODULE_STATUS_FILE%" >nul 2>&1

set "WILLPOWER_DIR=%ROOT_DIR%\ext\willpower"
if not exist "%WILLPOWER_DIR%\CMakeLists.txt" (
    set "ERROR_MESSAGE=Willpower was not checked out correctly"
    goto fatal
)
set "MPP_DIR=%WILLPOWER_DIR%\ext\massive-poly-pusher"
if not exist "%MPP_DIR%\CMakeLists.txt" (
    set "ERROR_MESSAGE=MassivePolyPusher was not checked out correctly"
    goto fatal
)
if not exist "%ROOT_DIR%\ext\yaml-cpp\CMakeLists.txt" (
    set "ERROR_MESSAGE=yaml-cpp was not checked out correctly"
    goto fatal
)
if not exist "%ROOT_DIR%\ext\nativefiledialog-extended\CMakeLists.txt" (
    set "ERROR_MESSAGE=Native File Dialog Extended was not checked out correctly"
    goto fatal
)
if not exist "%ROOT_DIR%\ext\googletest\CMakeLists.txt" (
    set "ERROR_MESSAGE=GoogleTest was not checked out correctly"
    goto fatal
)

if /i "%WITH_MPP_LFS%"=="true" (
    echo Downloading MassivePolyPusher Git LFS files...
    git -C "%MPP_DIR%" lfs install --local
    if errorlevel 1 (
        set "ERROR_MESSAGE=failed to initialize Git LFS for MassivePolyPusher"
        goto fatal
    )
    git -C "%MPP_DIR%" lfs pull
    if errorlevel 1 (
        set "ERROR_MESSAGE=failed to download MassivePolyPusher Git LFS files"
        goto fatal
    )
)

echo Removing previous Willpower and MassivePolyPusher build output...
if exist "%WILLPOWER_DIR%\build" rmdir /s /q "%WILLPOWER_DIR%\build"
if exist "%WILLPOWER_DIR%\build" (
    set "ERROR_MESSAGE=could not remove Willpower build directory: %WILLPOWER_DIR%\build"
    goto fatal
)
if exist "%MPP_DIR%\build" rmdir /s /q "%MPP_DIR%\build"
if exist "%MPP_DIR%\build" (
    set "ERROR_MESSAGE=could not remove MassivePolyPusher build directory: %MPP_DIR%\build"
    goto fatal
)

echo Configuring Willpower build tree...
cmake -S "%WILLPOWER_DIR%" -B "%WILLPOWER_DIR%\build"
if errorlevel 1 (
    set "ERROR_MESSAGE=Willpower CMake configuration failed"
    goto fatal
)
set "MULTI_CONFIG=false"
findstr /b /c:"CMAKE_CONFIGURATION_TYPES:" "%WILLPOWER_DIR%\build\CMakeCache.txt" >nul
if not errorlevel 1 set "MULTI_CONFIG=true"
if /i "%MULTI_CONFIG%"=="false" (
    echo Selecting Willpower %BUILD_TYPE% build type...
    cmake -S "%WILLPOWER_DIR%" -B "%WILLPOWER_DIR%\build" -DCMAKE_BUILD_TYPE="%BUILD_TYPE%"
    if errorlevel 1 (
        set "ERROR_MESSAGE=Willpower CMake configuration failed"
        goto fatal
    )
)

echo Building Willpower and its required MassivePolyPusher targets %BUILD_TYPE%...
cmake --build "%WILLPOWER_DIR%\build" --config "%BUILD_TYPE%" --parallel
if errorlevel 1 (
    set "ERROR_MESSAGE=Willpower build failed"
    goto fatal
)

rem Willpower's ExternalProject configures MassivePolyPusher below its own build
rem tree, while MassivePolyPusher writes artifacts below its source-tree build
rem directory. Build TungstenOxide's supplemental targets directly, in the same
rem order used by BooleanWorld, before configuring their native consumers.
echo Building supplemental MassivePolyPusher targets %BUILD_TYPE%...
cmake --build "%WILLPOWER_DIR%\build\_deps\massive-poly-pusher-build" --config "%BUILD_TYPE%" --parallel --target MppResourceParsers MppAppSupport assimp
if errorlevel 1 (
    set "ERROR_MESSAGE=MassivePolyPusher supplemental build failed"
    goto fatal
)

if not defined BUILD_DIR (
    set "ERROR_MESSAGE=refusing to remove an empty build directory"
    goto fatal
)
for %%I in ("%BUILD_DIR%") do set "BUILD_DIR=%%~fI"
if /i "%BUILD_DIR%"=="%ROOT_DIR%" (
    set "ERROR_MESSAGE=refusing to remove unsafe build directory: %BUILD_DIR%"
    goto fatal
)
for %%I in ("%BUILD_DIR%\..") do set "BUILD_PARENT=%%~fI"
if /i "%BUILD_DIR%"=="%BUILD_PARENT%" (
    set "ERROR_MESSAGE=refusing to remove unsafe build directory: %BUILD_DIR%"
    goto fatal
)

set "CHECK_DIR=%ROOT_DIR%"
:check_repository_parent
if /i "%CHECK_DIR%"=="%BUILD_DIR%" (
    set "ERROR_MESSAGE=refusing to remove a directory containing the TungstenOxide checkout: %BUILD_DIR%"
    goto fatal
)
for %%I in ("%CHECK_DIR%\..") do set "CHECK_PARENT=%%~fI"
if /i "%CHECK_DIR%"=="%CHECK_PARENT%" goto repository_parent_checked
set "CHECK_DIR=%CHECK_PARENT%"
goto check_repository_parent

:repository_parent_checked
set "CHECK_DIR=%BUILD_DIR%"
:check_protected_directory
if /i "%CHECK_DIR%"=="%WILLPOWER_DIR%" (
    set "ERROR_MESSAGE=TungstenOxide build directory must not be inside ext\willpower"
    goto fatal
)
for %%I in ("%CHECK_DIR%\..") do set "CHECK_PARENT=%%~fI"
if /i "%CHECK_DIR%"=="%CHECK_PARENT%" goto protected_directory_checked
set "CHECK_DIR=%CHECK_PARENT%"
goto check_protected_directory

:protected_directory_checked
echo Removing previous TungstenOxide build output...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%BUILD_DIR%" (
    set "ERROR_MESSAGE=could not remove build directory: %BUILD_DIR%"
    goto fatal
)

echo Configuring TungstenOxide %BUILD_TYPE% build in %BUILD_DIR%...
if /i "%MULTI_CONFIG%"=="true" (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%"
) else (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE="%BUILD_TYPE%"
)
if errorlevel 1 (
    set "ERROR_MESSAGE=TungstenOxide CMake configuration failed"
    goto fatal
)

echo Building TungstenOxide and supplemental MassivePolyPusher targets...
cmake --build "%BUILD_DIR%" --config "%BUILD_TYPE%" --parallel
if errorlevel 1 (
    set "ERROR_MESSAGE=TungstenOxide build failed"
    goto fatal
)

echo Build completed successfully.
if /i "%MULTI_CONFIG%"=="true" (
    echo Launcher:       %BUILD_DIR%\src\launcher\%BUILD_TYPE%\Launcher.exe
    echo Track editor:   %BUILD_DIR%\src\editor\%BUILD_TYPE%\track_editor.exe
    echo Model tool:     %BUILD_DIR%\src\model-tool\%BUILD_TYPE%\model_tool.exe
    echo glTF converter: %BUILD_DIR%\src\gltf-convert\%BUILD_TYPE%\gltf_convert.exe
) else (
    echo Launcher:       %BUILD_DIR%\src\launcher\Launcher.exe
    echo Track editor:   %BUILD_DIR%\src\editor\track_editor.exe
    echo Model tool:     %BUILD_DIR%\src\model-tool\model_tool.exe
    echo glTF converter: %BUILD_DIR%\src\gltf-convert\gltf_convert.exe
)
popd
exit /b 0

:usage_success
call :usage
exit /b 0

:usage
echo Usage: build_from_scratch.bat [options]
echo.
echo Build TungstenOxide, Willpower, MassivePolyPusher, and all other required
echo submodules from clean build trees.
echo.
echo Options:
echo   --with-mpp-lfs       Download MassivePolyPusher's Git LFS files.
echo   --config CONFIG      Build configuration ^(default: Release^).
echo   --build-dir DIR      TungstenOxide build directory, relative to this repository
echo                        unless absolute ^(default: build-windows^).
echo   -h, --help           Show this help.
echo.
echo Environment:
echo   CC, CXX               Select the C and C++ compilers during configuration.
echo   CMAKE_GENERATOR       Select a CMake generator.
echo   CMAKE_BUILD_PARALLEL_LEVEL
echo                         Limit the number of parallel build jobs.
exit /b 0

:fatal
if defined PUSHD_DONE popd
>&2 <nul set /p "=error: %ERROR_MESSAGE%"
>&2 echo.
exit /b 1
