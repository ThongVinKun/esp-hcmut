# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Users/Admin/esp/v5.3.1/esp-idf/components/bootloader/subproject"
  "S:/BKU/HK9/nhung/BTL1/build/bootloader"
  "S:/BKU/HK9/nhung/BTL1/build/bootloader-prefix"
  "S:/BKU/HK9/nhung/BTL1/build/bootloader-prefix/tmp"
  "S:/BKU/HK9/nhung/BTL1/build/bootloader-prefix/src/bootloader-stamp"
  "S:/BKU/HK9/nhung/BTL1/build/bootloader-prefix/src"
  "S:/BKU/HK9/nhung/BTL1/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "S:/BKU/HK9/nhung/BTL1/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "S:/BKU/HK9/nhung/BTL1/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
