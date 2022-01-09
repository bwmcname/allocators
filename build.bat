@echo off

set code=%cd%
set warn=-Wall -WX -wd4530 -wd4820 -wd4577 -wd5045 -wd4710 -wd4711

set drmemflags=/MT /EHsc /Oy- /Ob0

if not exist "bin" mkdir bin
pushd bin
cl %code%\test.cpp -Fetest.exe %warn% -Zi
popd
