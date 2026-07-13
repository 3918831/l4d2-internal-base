@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%

cl.exe /nologo /EHsc /std:c++17 /I src tests\PortalTransformTests.cpp src\Portal\PortalTransform.cpp src\Util\Math\Math.cpp /Fe:tests\PortalTransformTests.exe
if errorlevel 1 exit /b %errorlevel%

tests\PortalTransformTests.exe
