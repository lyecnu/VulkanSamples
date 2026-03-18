@echo off
setlocal enabledelayedexpansion

rem Build all .vert .frag .comp GLSL shaders to SPIR-V for Vulkan 1.0
rem Usage: build.bat [subfolder] [-d|debug]
rem   subfolder  - only compile shaders in this subfolder
rem   -d | debug - also compile debug variants (suffix: _debug)

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

rem Parse arguments: any order of [subfolder] [-d|debug]
set "FILTER="
set "BUILD_DEBUG=0"
:parse_args
if "%~1"=="" goto :done_args
if /I "%~1"=="debug" (set "BUILD_DEBUG=1") else if /I "%~1"=="-d" (set "BUILD_DEBUG=1") else (set "FILTER=%~1")
shift
goto :parse_args
:done_args

if defined FILTER (
	if "!BUILD_DEBUG!"=="1" (
		echo Compiling GLSL shaders to SPIR-V [release+debug] in folder: !FILTER!
	) else (
		echo Compiling GLSL shaders to SPIR-V [release] in folder: !FILTER!
	)
) else (
	if "!BUILD_DEBUG!"=="1" (
		echo Compiling GLSL shaders to SPIR-V [release+debug] in all subfolders
	) else (
		echo Compiling GLSL shaders to SPIR-V [release] in all subfolders
	)
)
echo.

for %%e in (vert frag comp) do (
	for /R "%CD%" %%f in (*.%%e) do (
		if /I NOT "%%~dpf"=="%CURDIR%" (
			rem Check subfolder filter
			set "skip=0"
			if defined FILTER (
				set "fpath=%%~dpf"
				set "fcheck=!fpath:%FILTER%=!"
				if "!fcheck!"=="!fpath!" set "skip=1"
			)
			if "!skip!"=="0" (
				set "full=%%f"
				set "rel=!full:%CURDIR%=!"
				if "!rel!"=="!full!" set "rel=%%~nxf"

				rem --- Release build ---
				set /a TOTAL+=1
				set "out=%%~dpf%%~nxf.spv"
				glslangValidator -V --target-env vulkan1.0 -o "!out!" "%%f" > temp_log.txt 2>&1
				findstr /C:"ERROR" temp_log.txt >nul
				if errorlevel 1 (
					set /a SUCCESS+=1
				) else (
					echo [RELEASE] !rel!:
					type temp_log.txt
					echo(
					set /a FAIL+=1
				)
				del temp_log.txt

				rem --- Debug build (-g: debug info, -Od: no optimization) ---
				if "!BUILD_DEBUG!"=="1" (
					set /a TOTAL+=1
					set "dbgout=%%~dpf%%~nf_debug%%~xf.spv"
					glslangValidator -V --target-env vulkan1.0 -g -Od -o "!dbgout!" "%%f" > temp_log.txt 2>&1
					findstr /C:"ERROR" temp_log.txt >nul
					if errorlevel 1 (
						set /a SUCCESS+=1
					) else (
						echo [DEBUG] !rel!:
						type temp_log.txt
						echo(
						set /a FAIL+=1
					)
					del temp_log.txt
				)
			)
		)
	)
)

echo(
echo Compilation summary:
echo   Total: !TOTAL!
echo   Success: !SUCCESS!
echo   Failed: !FAIL!

echo.
set /p dummy=Press Enter To Continue..
endlocal
