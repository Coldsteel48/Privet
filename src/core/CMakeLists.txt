add_library(facial_core STATIC
    camera/PixelFormat.cpp
    camera/CameraMode.cpp
    camera/V4L2Camera.cpp
    camera/CameraEnumerator.cpp
    camera/CameraSelection.cpp
    face/AngleBucket.cpp
    face/FaceDetector.cpp
    face/FaceEmbedder.cpp
    match/EmbeddingMatcher.cpp
    storage/EmbeddingFormat.cpp
    storage/EmbeddingStore.cpp
    config/Config.cpp
    log/Logger.cpp
    verify/VerifyRunner.cpp
    pam/PamServiceConfig.cpp
)

# Headers are included as "core/camera/Foo.hpp" etc. throughout the tree,
# so the include root is src/ (the parent of core/), not core/ itself.
target_include_directories(facial_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

# PkgConfig::OPENCV supplies include dirs/compile flags; linking uses the
# curated FACIAL_OPENCV_LIBRARIES list instead of OpenCV's full bundled
# library set — see the top-level CMakeLists.txt for why.
target_include_directories(facial_core PUBLIC ${OPENCV_INCLUDE_DIRS})
target_compile_options(facial_core PUBLIC ${OPENCV_CFLAGS_OTHER})
target_link_libraries(facial_core PUBLIC ${FACIAL_OPENCV_LIBRARIES} PkgConfig::LIBV4L2)
