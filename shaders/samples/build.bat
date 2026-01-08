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

for %%e in (vert frag comp) do (
	for /R "%CD%" %%f in (*.%%e) do (
		if /I NOT "%%~dpf"=="%CURDIR%" (
			set /a TOTAL+=1
			echo.
			set "full=%%f"
			set "rel=!full:%CURDIR%=!"
			if "!rel!"=="!full!" set "rel=%%~nxf"
			echo Compiling "!rel!"...
			set "out=%%~dpf%%~nxf.spv"
			set "outrel=!out:%CURDIR%=!"
			echo   Output: "!outrel!"
			glslangValidator -V --target-env vulkan1.3 -o "!out!" "%%f"
			if errorlevel 1 (
				echo FAILED: "!rel!"
				set /a FAIL+=1
				echo !rel!>>"%FAILED_FILE%"
			) else (
				set /a SUCCESS+=1
			)
		)
	)
)

echo.
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
pause
endlocal
