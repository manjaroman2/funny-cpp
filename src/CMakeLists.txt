add_executable(${PROJECT_NAME} main.cpp api.hpp)

target_link_libraries(${PROJECT_NAME} 
    PRIVATE pthread
)
