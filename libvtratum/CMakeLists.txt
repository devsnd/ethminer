set(SOURCES
    EthStratumClient.h EthStratumClient.cpp
    EthStratumClientV2.h EthStratumClientV2.cpp
)

add_library(ethvtratum ${SOURCES})
target_link_libraries(ethvtratum PUBLIC devcore Boost::system jsoncpp_lib_static)
target_include_directories(ethvtratum PRIVATE ..)
