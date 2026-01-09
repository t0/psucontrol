# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
find_package(Python REQUIRED COMPONENTS Interpreter)

project(psucontrol)

target_sources(app PRIVATE
  src/main.c
  src/psu.c
)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

target_link_libraries(app PRIVATE zephyr_interface zephyr)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(
  NAME http_resource_desc_psu_http_service
  KVMA RAM_REGION GROUP RODATA_REGION
)

foreach(web_resource
  index.html
  main.js
)
  generate_inc_file_for_target(app
    src/static_web_resources/${web_resource}
    ${gen_dir}/${web_resource}.gz.inc
    --gzip
  )
endforeach()
