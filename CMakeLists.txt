# Copyright (c) 2022 ARM Limited. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

add_library(hexiwear_kw40z INTERFACE)

target_include_directories(hexiwear_kw40z INTERFACE .)

target_sources(hexiwear_kw40z INTERFACE kw40z.cpp)
