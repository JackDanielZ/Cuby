cmake_minimum_required(VERSION 2.8.1)

project(Cuby)

find_package(Eina REQUIRED)
find_package(Eet REQUIRED)
find_package(Ecore REQUIRED)
find_package(Eo REQUIRED)
find_package(Efl REQUIRED)
find_package(Evas REQUIRED)
find_package(Elementary REQUIRED)

add_subdirectory(src)
