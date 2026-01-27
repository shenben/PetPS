add_library(base base.cc)
target_link_libraries(base PUBLIC Folly::folly ${CITYHASH_LIBRARIES})
target_include_directories(base PUBLIC ${CITYHASH_INCLUDE_DIRS})
# Add folly include directory for compatibility
target_include_directories(base PUBLIC "/home/pxg/folly-install/usr/include")
if(TARGET Cityhash)
	add_dependencies(base Cityhash)
endif()
