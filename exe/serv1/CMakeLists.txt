get_filename_component(_EXE_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_EXE_NAME}
    ${_EXE_NAME}.cpp
	)

    
target_link_libraries(${_EXE_NAME} libsupercloud)
