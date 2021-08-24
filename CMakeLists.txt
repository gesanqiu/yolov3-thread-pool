cmake_minimum_required(VERSION 3.10)

PROJECT(AiObject-Yolov3)

set(CMAKE_CXX_STANDARD 11)

set(SNPE_INC ${PROJECT_SOURCE_DIR}/inc/snpe/include/zdl)
set(SOURCE ${PROJECT_SOURCE_DIR}/src)

include(FindPkgConfig)
pkg_check_modules(GST            REQUIRED gstreamer-1.0)
pkg_check_modules(GSTAPP         REQUIRED gstreamer-app-1.0)
pkg_check_modules(GLIB           REQUIRED glib-2.0)
pkg_check_modules(JSON           REQUIRED json-glib-1.0)
pkg_check_modules(GFLAGS         REQUIRED gflags)

set(OpenCV_DIR "/opt/opencv-4.2.0/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)

include_directories(
    ${PROJECT_SOURCE_DIR}/inc
    ${SNPE_INC}
    ${OpenCV_INCLUDE_DIRS}
    ${GST_INCLUDE_DIRS}
    ${GSTAPP_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${GFLAGS_INCLUDE_DIRS}
    ${JSON_INCLUDE_DIRS}
    /usr/include/qrb5165
)

link_directories(
    ${OpenCV_LIBRARY_DIRS}/
    ${GST_LIBRARY_DIRS}
    ${GSTAPP_LIBRARY_DIRS}
    ${GLIB_LIBRARY_DIRS}
    ${JSON_LIBRARY_DIRS}
    ${PROJECT_SOURCE_DIR}/libs
    ${GFLAGS_LIBRARY_DIRS}
)

add_definitions(-DUSE_MODEL_FILE)

add_executable(${PROJECT_NAME}
    ${SOURCE}/VideoPipeline.cpp
    ${SOURCE}/PipelineInterface.cpp
    ${SOURCE}/YoloClassification.cpp
    ${SOURCE}/main.cpp
)

target_link_libraries(${PROJECT_NAME}
    ${PROJECT_SOURCE_DIR}/libs/aarch/libSNPE.so	
    ${OpenCV_LIBS}
    ${GST_LIBRARIES}
    ${GSTAPP_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${JSON_LIBRARIES}
    ${OpenCV_LIBRARIES}
    ${GFLAGS_LIBRARIES}
    pthread
    dl
    qtimlmeta
)
