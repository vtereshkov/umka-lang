#!/bin/sh
cd src
sh -c ../run_emscripten_linux.sh
cd ..

mkdir playground
mv src/umka.js playground
