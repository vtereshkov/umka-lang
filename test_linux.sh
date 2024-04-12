#!/bin/sh

cd tests

cd lib
./build_lib_linux.sh
cd ..

../umka_linux/umka -warn all.um > actual.log
../umka_linux/umka -warn compare.um actual.log expected.log
cd .. 

cd benchmarks
../umka_linux/umka -warn allbench.um > actual.log
../umka_linux/umka -warn ../tests/compare.um actual.log expected.log
cd ..
 