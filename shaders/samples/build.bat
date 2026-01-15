@echo off
setlocal enabledelayedexpansion

rem Build all .vert .frag .comp GLSL shaders to SPIR-V for Vulkan 1.3

where glslangValidator >nul 2>&1
if %errorlevel% neq 0 (
	echo glslangValidator not found in PATH.
	echo Please install the Vulkan SDK and add %%VULKAN_SDK%%\Bin to your PATH, or put glslangValidator.exe in PATH.
	pause
	exit /b 1
)

set "CURDIR=%CD%\"
set /a TOTAL=0
set /a SUCCESS=0
set /a FAIL=0
set "FAILED_FILE=%TEMP%\shader_build_failed.txt"
if exist "%FAILED_FILE%" del "%FAILED_FILE%" >nul 2>&1

echo Compiling GLSL shaders to SPIR-V (target: Vulkan 1.3)...
echo.

for %%e in (vert frag comp) do (
	for /R "%CD%" %%f in (*.%%e) do (
		if /I NOT "%%~dpf"=="%CURDIR%" (
			set /a TOTAL+=1
			set "full=%%f"
			set "rel=!full:%CURDIR%=!"
			if "!rel!"=="!full!" set "rel=%%~nxf"
			set "out=%%~dpf%%~nxf.spv"
			glslangValidator -V --target-env vulkan1.3 -o "!out!" "%%f" 2>&1 > temp_log.txt
			findstr /C:"ERROR" temp_log.txt >nul
			if errorlevel 1 (
				set /a SUCCESS+=1
			) else (
				type temp_log.txt
				echo(
				set /a FAIL+=1
			)
			del temp_log.txt
		)
	)
)

echo(
echo Compilation summary:
echo   Total: !TOTAL!
echo   Success: !SUCCESS!
echo   Failed: !FAIL!
if exist "%FAILED_FILE%" (
	echo.
	echo Failed files:
	type "%FAILED_FILE%"
)

echo.
set /p dummy=Press Enter To Continue..
endlocal
