cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# if(${IDF_VERSION_MAJOR} LESS "5")
#   include_directories("managed_components/joltwallet__littlefs/include")
# endif()

project(PsychicHttp_CppEspIdf) 