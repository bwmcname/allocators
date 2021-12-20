@echo off

set code=%cd%
set warn=-wd4530

if not exist "bin" mkdir bin
pushd bin
cl %code%\test.cpp -Fetest.exe %warn% -Zi
popd
