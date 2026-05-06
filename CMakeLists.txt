cmake_minimum_required(VERSION 3.30)
project(Controller)

# Enable hot reload for MSVC compiler if supported
if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif ()

# Add executable
add_executable(Controller WIN32)

# Set the generated executable file name
set_target_properties(Controller PROPERTIES OUTPUT_NAME Controller)

# set c++23 standard
target_compile_features(Controller PRIVATE cxx_std_23)

# Add file for executable
target_sources(Controller
        PRIVATE
        # resource files
        res/resource.rc
        res/app.manifest

        # main file
        src/main.cpp

        PRIVATE FILE_SET CXX_MODULES FILES
        src/common/Utils.ixx
        src/common/Common.ixx
        src/components/Config.ixx
        src/components/Controller.ixx
        src/components/I18n.ixx
        src/components/KernelService.ixx
        src/components/NetworkBlocker.ixx
        src/components/TrayManager.ixx
        src/profile/Downloader.ixx
        src/profile/Manager.ixx
)

# Add header file directories
target_include_directories(Controller PRIVATE include)

# Add Unicode definitions
target_compile_definitions(Controller PRIVATE UNICODE _UNICODE)

# Find libraries
find_package(rapidjson CONFIG REQUIRED)

# Link libraries
target_link_libraries(Controller PRIVATE
        rapidjson
        comsuppw.lib
)
