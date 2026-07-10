cmake_minimum_required(VERSION 3.30)
project(controller)

# Enable hot reload for MSVC compiler if supported
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Add executable
add_executable(controller WIN32)

# Set the generated executable file name
set_target_properties(controller PROPERTIES OUTPUT_NAME controller)

# UAC manifest: only the tray app requires administrator (for the firewall
# rule); test executables stay asInvoker so they can run unelevated
set_target_properties(controller PROPERTIES
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

# set c++23 standard
target_compile_features(controller PRIVATE cxx_std_23)

# Add file for executable
file(GLOB_RECURSE CPP_LIST CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE CPP_MODULE_LIST CONFIGURE_DEPENDS src/*.ixx)
set(RES_LIST res/resource.rc res/app.manifest)
target_sources(controller
        PRIVATE ${CPP_LIST} ${RES_LIST}
        PRIVATE FILE_SET CXX_MODULES FILES ${CPP_MODULE_LIST}
)

# Add header file directories
target_include_directories(controller PRIVATE include)

# Add Unicode definitions
target_compile_definitions(controller PRIVATE UNICODE _UNICODE)

# Find libraries
find_package(rapidjson CONFIG REQUIRED)

# Link libraries
target_link_libraries(controller PRIVATE
        rapidjson
        comsuppw.lib
)

# Unit tests: console executable reusing the modules under test
enable_testing()
add_executable(controller_tests)
target_sources(controller_tests
        PRIVATE tests/tests.cpp
        PRIVATE FILE_SET CXX_MODULES FILES
        src/common/Utils.ixx
        src/components/Log.ixx
        src/components/Config.ixx
)
target_include_directories(controller_tests PRIVATE include)
target_compile_definitions(controller_tests PRIVATE UNICODE _UNICODE)
target_compile_features(controller_tests PRIVATE cxx_std_23)
target_link_libraries(controller_tests PRIVATE rapidjson)
add_test(NAME controller_tests COMMAND controller_tests)
