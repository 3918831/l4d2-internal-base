@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%

cl.exe /nologo /EHsc /std:c++17 tests\PortalBspDataTests.cpp src\Portal\PortalBspData.cpp /Fe:tests\PortalBspDataTests.exe
if errorlevel 1 exit /b %errorlevel%

tests\PortalBspDataTests.exe
