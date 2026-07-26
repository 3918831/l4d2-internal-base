@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%

if not exist tests\obj\portal_render_fix_mesh_writer mkdir tests\obj\portal_render_fix_mesh_writer
cl.exe /nologo /EHsc /std:c++17 tests\PortalRenderFixMeshWriterTests.cpp src\Portal\PortalRenderFixMeshWriter.cpp /Fo:tests\obj\portal_render_fix_mesh_writer\ /Fe:tests\PortalRenderFixMeshWriterTests.exe
if errorlevel 1 exit /b %errorlevel%

tests\PortalRenderFixMeshWriterTests.exe
