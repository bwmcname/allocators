@echo off

set code=%cd%
set main=%code%\..\
set warn=-Wall -WX -wd4530 -wd4820 -wd4577 -wd5045 -wd4710 -wd4711
set flags=-I%main%

set drmemflags=/MT /EHsc /Oy- /Ob0

if not exist "%main%\bin" mkdir %main%\bin
pushd %main%\bin
cl %code%\test.cpp -Fetest.exe %flags% %warn% -Zi
popd
