# Own libraries
set(PRIVATE_COMPILE_OPTIONS -Wall -Wextra -Wno-sign-compare -Wno-implicit-fallthrough -fPIC)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(APPEND PRIVATE_COMPILE_OPTIONS -fext-numeric-literals -Wno-deprecated-declarations)
endif()

SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Common include directories for all reseq targets
set(RESEQ_INCLUDE_DIRS
  "${PROJECT_BINARY_DIR}"
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/reseq"
)

add_library(DataStats STATIC DataStats.cpp AdapterStats.cpp CoverageStats.cpp ErrorStats.cpp FragmentDistributionStats.cpp FragmentDuplicationStats.cpp QualityStats.cpp TileStats.cpp Reference.cpp Surrounding.cpp)
target_include_directories(DataStats PUBLIC ${RESEQ_INCLUDE_DIRS})
target_link_libraries(DataStats PUBLIC skewer_matrix nlopt vendored_seqan vendored_rootpwa GTest::gtest ${Boost_LIBRARIES} ZLIB::ZLIB BZip2::BZip2 rt)
target_compile_options(DataStats PRIVATE ${PRIVATE_COMPILE_OPTIONS})

add_library(DataStatsInterface STATIC DataStatsInterface.cpp)
target_link_libraries(DataStatsInterface PUBLIC DataStats)
target_compile_options(DataStatsInterface PRIVATE ${PRIVATE_COMPILE_OPTIONS})

# Executables
add_executable(reseq main.cpp ProbabilityEstimates.cpp Simulator.cpp AdapterStatsTest.cpp CoverageStatsTest.cpp DataStatsTest.cpp ErrorStatsTest.cpp FragmentDistributionStatsTest.cpp FragmentDuplicationStatsTest.cpp ProbabilityEstimatesTest.cpp QualityStatsTest.cpp ReferenceTest.cpp SeqQualityStatsTest.cpp SimulatorTest.cpp SurroundingTest.cpp TileStatsTest.cpp utilitiesTest.cpp VectTest.cpp)
target_link_libraries(reseq PRIVATE DataStats DataStatsInterface GTest::gtest vendored_seqan ${Boost_LIBRARIES} rt)
target_compile_options(reseq PRIVATE ${PRIVATE_COMPILE_OPTIONS})

install(TARGETS reseq
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib)
