@echo off
setlocal enabledelayedexpansion

echo ============================================
echo   NQE Windows Build - CURL Setup Script
echo ============================================
echo.

:: Create third_party directory if it doesn't exist
if not exist "third_party" mkdir third_party
cd third_party

:: Check if CURL is already downloaded
if exist "curl" (
    echo [INFO] CURL directory already exists. Skipping download.
    goto :build
)

echo [1/4] Downloading prebuilt CURL for Windows...
echo.

:: Download prebuilt CURL from curl.se
set CURL_URL=https://curl.se/windows/dl-8.10.1_1/curl-8.10.1_1-win64-mingw.zip
set CURL_ZIP=curl-8.10.1_1-win64-mingw.zip

powershell -Command "& {[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%CURL_URL%' -OutFile '%CURL_ZIP%'}"

if errorlevel 1 (
    echo [ERROR] Failed to download CURL from curl.se
    echo Please check your internet connection and try again.
    cd ..
    exit /b 1
)

echo [SUCCESS] Downloaded CURL successfully
echo.

echo [2/4] Extracting CURL...
echo.

:: Extract the zip file
powershell -Command "Expand-Archive -Path '%CURL_ZIP%' -DestinationPath '.' -Force"

if errorlevel 1 (
    echo [ERROR] Failed to extract CURL
    cd ..
    exit /b 1
)

:: Rename the extracted folder to "curl"
move curl-8.10.1_1-win64-mingw curl

:: Clean up zip file
del "%CURL_ZIP%"

echo [SUCCESS] Extracted CURL to third_party/curl
echo.

:build
cd ..

echo [3/4] Configuring CMake with CURL...
echo.

:: Detect Visual Studio
set "VS_PATH="
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2022"
) else if exist "C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2019"
) else (
    echo [ERROR] Visual Studio not found
    echo Please install Visual Studio 2019 or 2022
    exit /b 1
)

echo [INFO] Found Visual Studio %VS_VERSION%
echo.

:: Set up CURL paths
set "CURL_INCLUDE_DIR=%CD%\third_party\curl\include"
set "CURL_LIBRARY=%CD%\third_party\curl\lib\libcurl.dll.a"

echo [INFO] CURL Include: %CURL_INCLUDE_DIR%
echo [INFO] CURL Library: %CURL_LIBRARY%
echo.

:: Clean and recreate build directory to avoid generator conflicts
if exist "build_windows" (
    echo [INFO] Cleaning existing build directory...
    rmdir /s /q build_windows
    echo [INFO] Build directory cleaned
)
mkdir build_windows
cd build_windows

echo [INFO] Configuring CMake with Visual Studio 2022 and UTF-8 support...
echo.

:: Configure CMake with CURL paths using Visual Studio generator
cmake -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCURL_INCLUDE_DIR="%CURL_INCLUDE_DIR%" ^
    -DCURL_LIBRARY="%CURL_LIBRARY%" ^
    ..

if errorlevel 1 (
    echo [ERROR] CMake configuration failed
    cd ..
    exit /b 1
)

echo [SUCCESS] CMake configuration completed
echo.

echo [4/4] Building all targets...
echo.

:: Build with parallel compilation
cmake --build . --config Release -j 8

if errorlevel 1 (
    echo [ERROR] Build failed
    cd ..
    exit /b 1
)

echo [SUCCESS] Build completed successfully
echo.

:: Copy CURL DLL to output directory
echo [INFO] Copying CURL DLL to output directory...
copy "..\third_party\curl\bin\libcurl-x64.dll" "Release\" /Y

cd ..

echo.
echo ============================================
echo   Build Complete!
echo ============================================
echo.
echo Output directory: build_windows\Release
echo.
echo Test executables:
echo   - feature_test.exe
echo   - network_change_test.exe
echo   - throughput_analyzer_test.exe
echo   - libcurl_multi_nqe_example.exe
echo   - And more...
echo.
echo To run tests: cd build_windows\Release ^&^& feature_test.exe
echo.

endlocal
