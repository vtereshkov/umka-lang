#!/bin/sh

cd tests

cd lib
./build_lib_darwin.sh
cd ..

../umka_darwin/umka -warn all.um > actual.log
../umka_darwin/umka -warn compare.um actual.log expected.log
cd .. 

cd benchmarks
../umka_darwin/umka -warn allbench.um > actual.log
../umka_darwin/umka -warn ../tests/compare.um actual.log expected.log
cd ..
