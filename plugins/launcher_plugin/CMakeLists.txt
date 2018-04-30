file(GLOB HEADERS "include/eosio/launcher_plugin/*.hpp")
add_library( launcher_plugin         
             eosd_def.cpp
             host_def.cpp
             launcher_def.cpp
             launcher_plugin.cpp
             ${HEADERS} )

target_link_libraries( launcher_plugin eosio_chain fc appbase)
target_include_directories( launcher_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
