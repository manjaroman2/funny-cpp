#[[
file(GLOB ASYNC_SOCKETS_SRC_FILES CMAKE_CONFIGURE_DEPENDS
    ${ASYNC_SOCKETS_INCLUDE}/*.hpp
)
]]

target_include_directories(${PROJECT_NAME}
    PUBLIC ${CMAKE_CURRENT_LIST_DIR}/async-sockets-cpp/async-sockets/include
)
