#!/bin/sh

cd tests

cd lib
./build_lib_linux.sh
cd ..

../umka_linux/umka all.um > actual.log
../umka_linux/umka compare.um actual.log expected.log
cd .. 