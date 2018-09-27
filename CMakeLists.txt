cmake_minimum_required(VERSION 3.10)

project(logger LANGUAGES CXX)

file(GLOB_RECURSE ${PROJECT_NAME}_SOURCES 
    "src/*.cpp"
)

add_library(${PROJECT_NAME}
    STATIC
        ${${PROJECT_NAME}_SOURCES}
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_14)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        include
)