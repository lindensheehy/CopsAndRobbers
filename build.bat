@echo off
echo Compiling...

:: Create the obj directory if it doesn't exist
if not exist obj mkdir obj

:: Compile all .cpp files in the src directory (and current directory if needed)
:: Note: If you have subdirectories inside src/, we can adjust this, but for flat src/ this is fastest.
g++ -Wall -std=c++17 -Iinclude src\*.cpp -o k_cops.exe

:: Check if compilation failed
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b %errorlevel%
)

echo Build successful. Running main.exe...
echo.
