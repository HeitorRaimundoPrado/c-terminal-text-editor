cmake_minimum_required (VERSION 3.16.3)
project(brterm)
add_executable(breditor main.c)
target_link_libraries(breditor ncurses)
