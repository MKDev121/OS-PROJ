# CMake generated Testfile for 
# Source directory: /home/meet-kareliya/Meet/Academics/os/project/OS-PROJ
# Build directory: /home/meet-kareliya/Meet/Academics/os/project/OS-PROJ/build_asan
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(ConfigUnitTests "/home/meet-kareliya/Meet/Academics/os/project/OS-PROJ/build_asan/unit_tests")
set_tests_properties(ConfigUnitTests PROPERTIES  _BACKTRACE_TRIPLES "/home/meet-kareliya/Meet/Academics/os/project/OS-PROJ/CMakeLists.txt;111;add_test;/home/meet-kareliya/Meet/Academics/os/project/OS-PROJ/CMakeLists.txt;0;")
subdirs("third_party/glfw")
