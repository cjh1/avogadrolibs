find_package(protobuf REQUIRED NO_MODULE)
include_directories(SYSTEM ${protobuf_INCLUDE_DIRS})

find_package(ProtoCall REQUIRED NO_MODULE)
include_directories(SYSTEM ${ProtoCall_INCLUDE_DIRS})

find_package(VTK COMPONENTS vtkParallelCore)
set_property(DIRECTORY APPEND PROPERTY COMPILE_DEFINITIONS ${VTK_DEFINITIONS})
include_directories(${VTK_INCLUDE_DIRS})

protocallc(_protocall_common_source
  _protocall_service_source
  _protocall_proxy_source
  _protocall_include_dir
  "remote.proto")

message("common: ${_protocall_common_source}")

include_directories(${_protocall_include_dir}
  ${CMAKE_SOURCE_DIR}/avogadro/protocall
  ${CMAKE_BINARY_DIR}/avogadro/protocall)

set(_server_source
  avogadroserver.cpp
  avoremotemoleculeservice.cpp
  )

add_executable(avoserver
  ${_protocall_common_source}
  ${_protocall_service_source}
  ${_server_source}
  )

target_link_libraries(avoserver
  vtkParallelCore AvogadroProtoCall)

foreach(_file ${_protocall_common_source} ${_protocall_proxy_source})
  set_source_files_properties(${_file} PROPERTIES SKIP_AUTOMOC TRUE)
endforeach()

set(_plugin_src "remotemolecule.cpp;connectionsettingsdialog.cpp")
list(APPEND _plugin_src  ${_protocall_common_source})
list(APPEND _plugin_src ${_protocall_proxy_source})

avogadro_plugin(RemoteMolecule
  "Open molecule on remote server."
  ExtensionPlugin
  "remotemolecule.h"
  RemoteMolecule
  "${_plugin_src}"
  "connectionsettingsdialog.ui"
  )

target_link_libraries(RemoteMolecule
  AvogadroIO
  vtkParallelCore
  AvogadroProtoCall
  )

