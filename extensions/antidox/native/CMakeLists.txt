set(KPPMODS_JAVA_HOME "$ENV{JAVA_HOME}" CACHE PATH "JDK used to build Antidox Java display helpers")
find_program(KPPMODS_JAVAC_EXECUTABLE javac HINTS "${KPPMODS_JAVA_HOME}/bin"
    NO_CMAKE_FIND_ROOT_PATH REQUIRED)
if(NOT KPPMODS_JAVA_HOME)
    get_filename_component(javac_real "${KPPMODS_JAVAC_EXECUTABLE}" REALPATH)
    get_filename_component(java_bin "${javac_real}" DIRECTORY)
    get_filename_component(KPPMODS_JAVA_HOME "${java_bin}" DIRECTORY)
endif()
find_path(KPPMODS_JNI_INCLUDE_DIR jni.h HINTS "${KPPMODS_JAVA_HOME}/include"
    NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_path(KPPMODS_JNI_PLATFORM_INCLUDE_DIR jni_md.h
    HINTS "${KPPMODS_JNI_INCLUDE_DIR}/linux" "${KPPMODS_JNI_INCLUDE_DIR}/darwin"
    NO_CMAKE_FIND_ROOT_PATH REQUIRED)
set(java_classes "${CMAKE_CURRENT_BINARY_DIR}/java")
set(java_header "${CMAKE_CURRENT_BINARY_DIR}/java-classes.hpp")
set(java_sources "${CMAKE_CURRENT_SOURCE_DIR}/java/Patches.java"
    "${CMAKE_CURRENT_SOURCE_DIR}/java/Display.java")
add_custom_command(OUTPUT "${java_header}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${java_classes}"
    COMMAND "${KPPMODS_JAVAC_EXECUTABLE}" -source 8 -target 8 -Xlint:-options -g:none
        -d "${java_classes}" ${java_sources}
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/embed.py"
        "${java_classes}" "${java_header}"
    DEPENDS ${java_sources} "${CMAKE_CURRENT_SOURCE_DIR}/embed.py" VERBATIM)
add_library(kppmods-antidox-framework SHARED framework.cpp "${java_header}")
set_target_properties(kppmods-antidox-framework PROPERTIES CXX_STANDARD 17
    LIBRARY_OUTPUT_DIRECTORY "${KPPMODS_NATIVE_OUTPUT}/modules")
target_include_directories(kppmods-antidox-framework PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${KPPMODS_JNI_INCLUDE_DIR}" "${KPPMODS_JNI_PLATFORM_INCLUDE_DIR}")
target_link_libraries(kppmods-antidox-framework PRIVATE kppmods-build dl)
