@echo off

set code=%cd%
set main=%code%\..\
set warn=-Wall -WX -wd4530 -wd4820 -wd4577 -wd5045 -wd4710 -wd4711
set flags=-I%code%\..\

set drmemflags=/MT /EHsc /Oy- /Ob0

if not exist "%main%\bin" mkdir %main%\bin
pushd %main%\bin
cl %code%\best_fit_single_comp.cpp -FeBESTFITCOMPSUCCESS.exe %flags% %warn% -Zi -O2
popd
