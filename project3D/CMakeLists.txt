cmake_minimum_required (VERSION 3.9)

# Kompilacja shaderów HLSL
add_custom_target(shaders)

set(SHADER_FILES VertexShader.hlsl PixelShader.hlsl)
set(SHADER_HEADERS "")

set_source_files_properties(VertexShader.hlsl PROPERTIES ShaderType "vs")
set_source_files_properties(PixelShader.hlsl PROPERTIES ShaderType "ps")
set_source_files_properties(${SHADER_FILES} PROPERTIES ShaderModel "5_1")

foreach(FILE ${SHADER_FILES})
    get_filename_component(FILE_WE ${FILE} NAME_WE)
    get_source_file_property(shadertype ${FILE} ShaderType)
    get_source_file_property(shadermodel ${FILE} ShaderModel)

    # Zmiana nazwy pliku z PascalCase na snake_case
    string(SUBSTRING ${FILE_WE} 0 1 FILE_WE_TEMP_1)
    string(SUBSTRING ${FILE_WE} 1 -1 FILE_WE_TEMP_2)
    string(TOLOWER ${FILE_WE_TEMP_1} FILE_WE_TEMP_1)
    string(CONCAT FILE_WE_CAMEL_CASE ${FILE_WE_TEMP_1} ${FILE_WE_TEMP_2})
    string(REGEX REPLACE "([A-Z])" "_\\1" FILE_WE_PASCAL_SNAKE_CASE ${FILE_WE_CAMEL_CASE})
    string(TOLOWER ${FILE_WE_PASCAL_SNAKE_CASE} FILE_WE_SNAKE_CASE)

    add_custom_command(TARGET shaders
            COMMAND fxc.exe /nologo /Emain /T${shadertype}_${shadermodel} $<IF:$<CONFIG:DEBUG>,/Od,/O1> /Vn ${shadertype}_main /Fh ../project3D/${FILE_WE_SNAKE_CASE}.h ${FILE}
            MAIN_DEPENDENCY ${FILE}
            COMMENT "HLSL ${FILE}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            VERBATIM)

    list(APPEND ${SHADER_HEADERS} "${FILE_WE_SNAKE_CASE}.h")
endforeach(FILE)


# Dodaj źródło do pliku wykonywalnego tego projektu.
add_executable (project3D
        "main.cpp"
        "app.cpp" "app.h"
        "object_loader.cpp" "object_loader.h"
        "d3dx12.h" "common.h"
        # TODO: DELETE LATER
        "pixel_shader.h" "vertex_shader.h"
        # ${SHADER_HEADERS}
)

add_dependencies(project3D shaders)

if (CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET project3D PROPERTY CXX_STANDARD 20)
endif()

# Szukanie biblioteki Direct3D (d3d12.lib)
find_library(DIRECT3D d3d12)
if (NOT DIRECT3D)
    message(FATAL_ERROR "Could not find Direct3D.")
endif ()

# Dołączenie biblioteki Direct3D
target_link_libraries(project3D ${DIRECT3D})

# Szukanie biblioteki DXGI (dxgi.lib)
find_library(DXGI dxgi)
if (NOT DXGI)
    message(FATAL_ERROR "Could not find DXGI.")
endif ()

# Dołączenie biblioteki DXGI
target_link_libraries(project3D ${DXGI})

# Szukanie biblioteki DXGUID (dxguid.lib)
find_library(DXGUID dxguid)
if (NOT DXGUID)
    message(FATAL_ERROR "Could not find DXGUID.")
endif ()

# Dołączenie biblioteki DXGUID
target_link_libraries(project3D ${DXGUID})

# Szukanie biblioteki WindowsCodecs (windowscodecs.lib)
find_library(WINDOWS_CODECS windowscodecs)
if (NOT WINDOWS_CODECS)
    message(FATAL_ERROR "Could not find WindowsCodecs.")
endif ()

# Dołączenie biblioteki WindowsCodecs
target_link_libraries(project3D ${WINDOWS_CODECS})
