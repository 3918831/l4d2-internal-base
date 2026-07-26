@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%

if not exist tests\obj\portal_bsp_binding mkdir tests\obj\portal_bsp_binding
cl.exe /nologo /EHsc /std:c++17 tests\PortalBspBindingTests.cpp src\Portal\PortalBspCollisionCarver.cpp src\Portal\PortalBspData.cpp src\Portal\PortalBspQuery.cpp /Fo:tests\obj\portal_bsp_binding\ /Fe:tests\PortalBspBindingTests.exe
if errorlevel 1 exit /b %errorlevel%

tests\PortalBspBindingTests.exe
