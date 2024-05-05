#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "FluidSynth::fluidsynth" for configuration "Debug"
set_property(TARGET FluidSynth::fluidsynth APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(FluidSynth::fluidsynth PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/tools/fluidsynth/fluidsynth.exe"
  )

list(APPEND _cmake_import_check_targets FluidSynth::fluidsynth )
list(APPEND _cmake_import_check_files_for_FluidSynth::fluidsynth "${_IMPORT_PREFIX}/tools/fluidsynth/fluidsynth.exe" )

# Import target "FluidSynth::libfluidsynth" for configuration "Debug"
set_property(TARGET FluidSynth::libfluidsynth APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(FluidSynth::libfluidsynth PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/debug/lib/fluidsynth.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/bin/libfluidsynth-3.dll"
  )

list(APPEND _cmake_import_check_targets FluidSynth::libfluidsynth )
list(APPEND _cmake_import_check_files_for_FluidSynth::libfluidsynth "${_IMPORT_PREFIX}/debug/lib/fluidsynth.lib" "${_IMPORT_PREFIX}/debug/bin/libfluidsynth-3.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
