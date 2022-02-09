#!/bin/bash

cmake .. \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DBUILD_ARM=on -DCOMPILE_CUDA=off \
    -DBUILD_ARCH=native \
    -DUSE_INTGEMM=off \
    -DUSE_SIMDE=ON

