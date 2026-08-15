add_executable(pam_test_harness main.cpp)
target_include_directories(pam_test_harness PRIVATE ${PAM_INCLUDE_DIR})
target_link_libraries(pam_test_harness PRIVATE ${PAM_LIBRARY})
# Test-only tool, never installed.
