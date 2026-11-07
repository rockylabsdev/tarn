# Helpers that let parallel tickets add pieces without touching shared lines.

# tarn_add_subdirectories(<dir>): add every direct subdirectory of <dir> that has
# a CMakeLists.txt. CONFIGURE_DEPENDS re-runs the glob on every build, so a new
# directory is picked up without a manual cmake run.
function(tarn_add_subdirectories dir)
  file(GLOB children CONFIGURE_DEPENDS LIST_DIRECTORIES true "${dir}/*")
  list(SORT children)
  foreach(child IN LISTS children)
    if(IS_DIRECTORY "${child}" AND EXISTS "${child}/CMakeLists.txt")
      add_subdirectory("${child}")
    endif()
  endforeach()
endfunction()

# tarn_core_module(): called from src/core/<name>/CMakeLists.txt. Adds every
# .cpp and .h in that directory to tarn_core. Most modules need nothing else.
function(tarn_core_module)
  file(GLOB srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
  target_sources(tarn_core PRIVATE ${srcs})
endfunction()

# tarn_add_test(<area> [LIBS ...] [TIMEOUT <s>]): called from tests/<area>/CMakeLists.txt.
# TIMEOUT (default 60) is a hang guard; raise it for areas that run real encoders,
# which slow down a lot when several builds share the machine.
# Builds one executable from every .cpp in the directory, links the harness and
# registers it with CTest in the safe environment (offscreen, no Hyprland).
function(tarn_add_test area)
  cmake_parse_arguments(ARG "" "TIMEOUT" "LIBS" ${ARGN})
  if(NOT ARG_TIMEOUT)
    set(ARG_TIMEOUT 60)
  endif()
  file(GLOB srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
  set(target test_${area})
  add_executable(${target} ${srcs} "${PROJECT_SOURCE_DIR}/tests/support/force_offscreen.cpp")
  target_link_libraries(${target} PRIVATE tarn_core tarn_harness Qt6::Test ${ARG_LIBS})
  add_test(NAME ${area} COMMAND ${target})
  set_tests_properties(${area} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QPA_PLATFORMTHEME=generic;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT ${ARG_TIMEOUT})
endfunction()
