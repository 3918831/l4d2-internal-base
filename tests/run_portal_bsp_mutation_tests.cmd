@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%

if not exist tests\obj\portal_bsp_mutation mkdir tests\obj\portal_bsp_mutation
cl.exe /nologo /EHsc /std:c++17 tests\PortalBspMutationTests.cpp src\Portal\PortalBspCollisionCarver.cpp src\Portal\PortalBspData.cpp src\Portal\PortalBspQuery.cpp /Fo:tests\obj\portal_bsp_mutation\ /Fe:tests\PortalBspMutationTests.exe
if errorlevel 1 exit /b %errorlevel%

tests\PortalBspMutationTests.exe
