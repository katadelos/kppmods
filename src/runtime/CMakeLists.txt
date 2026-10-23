file(GLOB_RECURSE BOOST_PROCESS_SOURCES CONFIGURE_DEPENDS "${KPPMODS_BOOST_ROOT}/libs/process/src/*.cpp")
add_library(kppmods-boost-process STATIC ${BOOST_PROCESS_SOURCES})
# Kindle kernels predate pidfd_open; use Boost.Process POSIX/SIGCHLD support.
target_compile_definitions(kppmods-boost-process PUBLIC BOOST_PROCESS_V2_DISABLE_PIDFD_OPEN BOOST_PROCESS_USE_STD_FS BOOST_PROCESS_STATIC_LINK _GLIBCXX_USE_CXX11_ABI=0 PRIVATE BOOST_PROCESS_SOURCE=1)
target_include_directories(kppmods-boost-process SYSTEM PUBLIC ${KPPMODS_BOOST_INCLUDE_DIRS})
add_executable(kppmods-runtime
    main.cpp broker.cpp actions.cpp commands.cpp service.cpp development.cpp
    settings.cpp status-items.cpp storage.cpp activation.cpp apps.cpp
    configuration.cpp device.cpp job-configuration.cpp jobs.cpp packages.cpp
    process.cpp diagnostics.cpp child-process.cpp filesystem.cpp hermes/discovery.cpp
    runtime-cache.cpp userstore.cpp
)
target_link_libraries(kppmods-runtime PRIVATE
    kppmods-json-0 kppmods-boost-process dl pthread rt)
target_link_options(kppmods-runtime PRIVATE -static-libstdc++ -static-libgcc)
set_target_properties(kppmods-runtime PROPERTIES OUTPUT_NAME kppmod
    RUNTIME_OUTPUT_DIRECTORY "${KPPMODS_OUTPUT}/bin")
target_include_directories(kppmods-runtime SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/vendor/utfcpp/source")
