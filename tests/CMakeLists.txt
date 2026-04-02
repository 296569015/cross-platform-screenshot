# Tests (Phase 1: core module unit tests only)
# Requires a test framework — using a minimal main for now.
# TODO: Add Google Test via vcpkg when needed.

add_executable(sst_tests
    test_state_machine.cpp
)

target_link_libraries(sst_tests PRIVATE
    core
    platform_interfaces
)

add_test(NAME StateMachineTests COMMAND sst_tests)
