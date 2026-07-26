@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%

if not exist tests\obj\portal_render_fix_geometry mkdir tests\obj\portal_render_fix_geometry
cl.exe /nologo /EHsc /std:c++17 tests\PortalRenderFixGeometryTests.cpp src\Portal\PortalRenderFixGeometry.cpp /Fo:tests\obj\portal_render_fix_geometry\ /Fe:tests\PortalRenderFixGeometryTests.exe
if errorlevel 1 exit /b %errorlevel%

tests\PortalRenderFixGeometryTests.exe
