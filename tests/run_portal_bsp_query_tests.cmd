@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%

if not exist tests\obj\portal_bsp_query mkdir tests\obj\portal_bsp_query
cl.exe /nologo /EHsc /std:c++17 tests\PortalBspQueryTests.cpp src\Portal\PortalBspQuery.cpp /Fo:tests\obj\portal_bsp_query\ /Fe:tests\PortalBspQueryTests.exe
if errorlevel 1 exit /b %errorlevel%

tests\PortalBspQueryTests.exe
