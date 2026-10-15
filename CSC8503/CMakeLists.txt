set(PROJECT_NAME CSC8503)

################################################################################
# Source groups
################################################################################
set(Header_Files
    "GameTechRendererInterface.h"
    "NetworkedGame.h"
    "NetworkPlayer.h"
    "StateGameObject.h"
    "TutorialGame.h"
)
source_group("Header Files" FILES ${Header_Files})

set(Source_Files
    "GameTechRendererInterface.cpp"
    "Main.cpp"
    "NetworkedGame.cpp"
    "NetworkPlayer.cpp"
    "StateGameObject.cpp"
    "TutorialGame.cpp"
)

if(USE_VULKAN)
    list(APPEND Header_Files "GameTechVulkanRenderer.h")
    list(APPEND Source_Files "GameTechVulkanRenderer.cpp")

    file(GLOB_RECURSE ALL_SHADERS CONFIGURE_DEPENDS
	    ${ASSET_ROOT}Shaders/VK/*.vert
	    ${ASSET_ROOT}Shaders/VK/*.frag 
	    ${ASSET_ROOT}Shaders/VK/*.comp 
	    ${ASSET_ROOT}Shaders/VK/*.geom 
	    ${ASSET_ROOT}Shaders/VK/*.tesc 
	    ${ASSET_ROOT}Shaders/VK/*.tese 
	    ${ASSET_ROOT}Shaders/VK/*.rgen 
	    ${ASSET_ROOT}Shaders/VK/*.rint 
	    ${ASSET_ROOT}Shaders/VK/*.rahit 
	    ${ASSET_ROOT}Shaders/VK/*.rchit 
	    ${ASSET_ROOT}Shaders/VK/*.rmiss 
	    ${ASSET_ROOT}Shaders/VK/*.rcall
        ${ASSET_ROOT}Shaders/VK/*.hlsl
	    ${ASSET_ROOT}Shaders/VK/*.task 
	    ${ASSET_ROOT}Shaders/VK/*.mesh)

    file(GLOB_RECURSE SHADER_HEADERS CONFIGURE_DEPENDS
	    ${ASSET_ROOT}Shaders/VK/*.glslh
	    ${ASSET_ROOT}Shaders/VK/*.glsli
        ${ASSET_ROOT}Shaders/VK/*.hlsli
	    ${ASSET_ROOT}Shaders/VK/*.h)

    source_group("Shader Files" FILES ${ALL_SHADERS} ${SHADER_HEADERS})

    set_source_files_properties(${SHADER_HEADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

elseif(USE_OPENGL)
    list(APPEND Header_Files "GameTechOpenGLRenderer.h")
    list(APPEND Source_Files "GameTechOpenGLRenderer.cpp")
endif()

source_group("Source Files" FILES ${Source_Files})

set(ALL_FILES
    ${Header_Files}
    ${Source_Files}
)

if(USE_VULKAN)
    set(APPEND ALL_FILES 
	    ${ALL_SHADERS}
	    ${SHADER_HEADERS}
    )
    message("Adding custom shader compiler commands...")
    foreach (file ${ALL_SHADERS})
        get_filename_component(file_name ${file} NAME)
	    get_filename_component(file_ext ${file} EXT)
	
	    if(file_ext STREQUAL ".h" OR file_ext STREQUAL ".cpp")
		    continue()
	    endif()
		
	    if( file_ext STREQUAL  ".vert" OR
		    file_ext STREQUAL  ".frag" OR
		    file_ext STREQUAL  ".comp" OR
		    file_ext STREQUAL  ".geom" OR
		    file_ext STREQUAL  ".tesc" OR
		    file_ext STREQUAL  ".tese" OR
		    file_ext STREQUAL  ".rgen" OR
		    file_ext STREQUAL  ".rint" OR
		    file_ext STREQUAL  ".rahit" OR
		    file_ext STREQUAL  ".rchit" OR
		    file_ext STREQUAL  ".rmiss" OR
		    file_ext STREQUAL  ".rcall" OR
		    file_ext STREQUAL  ".task" OR
		    file_ext STREQUAL  ".mesh"
	    )
		    message("Adding custom command to ${file}")
		    get_filename_component(file_dir ${file} ABSOLUTE)
            set(SPIRV_OUTPUT ${file_name}.spv)
		    set(SPIRV_ABS_INPUT ${file_dir})
		    set(SPIRV_ABS_OUTPUT ${file_dir}.spv)

		    add_custom_command(
			    OUTPUT ${SPIRV_ABS_OUTPUT}
			
			    COMMENT "Compiling GLSL shader:"
			    COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V  ${SPIRV_ABS_INPUT} -o ${SPIRV_ABS_OUTPUT}
                DEPENDS ${file} ${SHADER_HEADERS}
			    VERBATIM
		    )
            list(APPEND SPIRV_BINARY_FILES ${SPIRV_ABS_OUTPUT})   
	    endif()
    endforeach()
endif()

################################################################################
# Target
################################################################################
add_executable(${PROJECT_NAME}  ${ALL_FILES})

use_props(${PROJECT_NAME} "${CMAKE_CONFIGURATION_TYPES}" "${DEFAULT_CXX_PROPS}")
set(ROOT_NAMESPACE CSC8503)

#add_custom_target(
#    Shaders
#    DEPENDS ${SPIRV_BINARY_FILES}
#)
#
#add_dependencies(${PROJECT_NAME}
#    ${PROJECT_DEPENDENCIES}
#    Shaders
#)
#



################################################################################
# Compile definitions
################################################################################
if(MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        "UNICODE;"
        "_UNICODE" 
        "WIN32_LEAN_AND_MEAN"
        "_WINSOCKAPI_"   
        "_WINSOCK2API_"
        "_WINSOCK_DEPRECATED_NO_WARNINGS"
    )
endif()

target_precompile_headers(${PROJECT_NAME} PRIVATE
    <list>
    <vector>
    <map>
    <stack>
    <set>

    <string>
    <chrono>
    <thread>
    <atomic>
    <functional>
    <iostream>
    
    "../NCLCoreClasses/Vector.h"
    "../NCLCoreClasses/Quaternion.h"
    "../NCLCoreClasses/Plane.h"
    "../NCLCoreClasses/Matrix.h"
    "../NCLCoreClasses/GameTimer.h" 

    "../NCLCoreClasses/Window.h" 
)

if(WIN32)
    target_precompile_headers(${PROJECT_NAME} PRIVATE <windows.h> )
endif()
 
if(USE_VULKAN)
    target_precompile_headers(${PROJECT_NAME} PRIVATE
        <vulkan/vulkan.hpp>

        "../VKQuick/Instance.h"
        "../VKQuick/Texture.h"

        "../VKQuick/Mesh.h"
        "../VKQuick/PipelineBuilder.h"
        "../VKQuick/DynamicRenderBuilder.h"

        "../VKQuick/DescriptorSetLayoutBuilder.h"
        "../VKQuick/RenderPassBuilder.h"

        "../VKQuick/ComputePipelineBuilder.h"
    )
endif()

include_directories("../NCLCoreClasses/")
include_directories("../CSC8503CoreClasses/")

target_link_libraries(${PROJECT_NAME} LINK_PUBLIC NCLCoreClasses)
target_link_libraries(${PROJECT_NAME} LINK_PUBLIC CSC8503CoreClasses)
target_link_libraries(${PROJECT_NAME} LINK_PUBLIC GLTFLoader)

if(USE_VULKAN)
    add_compile_definitions(VULKAN_HPP_NO_STRUCT_CONSTRUCTORS)
    include_directories("../VulkanRendering/")
    include_directories("../VKQuick/")
    target_link_libraries(${PROJECT_NAME} LINK_PUBLIC VulkanRendering)
    target_link_libraries(${PROJECT_NAME} LINK_PUBLIC VKQuick)

   add_custom_target(
      Shaders
      DEPENDS ${SPIRV_BINARY_FILES}
   )
   add_dependencies(${PROJECT_NAME}
        ${PROJECT_DEPENDENCIES}
        Shaders
   )

   #target_link_libraries(${PROJECT_NAME} LINK_PUBLIC Shaders)

elseif(USE_OPENGL)
    include_directories("../OpenGLRendering/")
    target_link_libraries(${PROJECT_NAME} LINK_PUBLIC OpenGLRendering)
endif()


target_include_directories (${PROJECT_NAME} 
    PUBLIC ${Vulkan_INCLUDE_DIR}
    PUBLIC ${CMAKE_SOURCE_DIR}/NCLCoreClasses
    PUBLIC ${CMAKE_SOURCE_DIR}/GLTFLoader
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC ${ASSET_ROOT}
)

target_link_libraries(${PROJECT_NAME} 
   PRIVATE ${PROJECT_DEPENDENCIES}
)