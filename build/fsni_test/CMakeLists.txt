set (FSNI_TEST_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set (FSNI_TEST_SRC ${FSNI_TEST_SRC_DIR}/main.cpp)

# set (FSNI_TEST_INC_DIR ${FSNI_TEST_SRC_DIR}/../../)

include_directories ("${FSNI_TEST_SRC_DIR}")
# include_directories ("${FSNI_TEST_INC_DIR}")

add_executable (fsni_test ${FSNI_TEST_SRC}) 

set (FSNI_TEST_LDLIBS xlua)

target_link_libraries (fsni_test ${FSNI_TEST_LDLIBS})

add_custom_command(TARGET fsni_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE_DIR:xlua>/xlua.dll
        $<TARGET_FILE_DIR:fsni_test>/
    COMMENT "copying xlua.dll...\n"
        )

add_custom_command(TARGET fsni_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/r.zip
        $<TARGET_FILE_DIR:fsni_test>/../
        )
        