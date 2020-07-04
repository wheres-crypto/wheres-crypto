# cmake -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 2.8)
project (wherescrypto C CXX)

set(CMAKE_CONFIGURATION_TYPES "Release" "Debug" CACHE STRING "" FORCE)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_path(IdaSdk_DIR NAMES include/ida.hpp
	HINTS ${IdaSdk_ROOT_DIR} ENV IDASDK_ROOT
	PATHS C:/idasdk C:/idasdk72
	PATH_SUFFIXES idasdk idasdk72
	DOC "Location of the IDA SDK"
	NO_DEFAULT_PATH)
set(IdaSdk_INCLUDE_DIRS ${IdaSdk_DIR}/include)

if (${IdaSdk_DIR} STREQUAL "IdaSdk_DIR-NOTFOUND")
	message(FATAL_ERROR "IDA SDK not found, try setting IdaSdk_ROOT_DIR")
endif()

find_path(QtConfig_DIR
	NAMES lib/cmake/Qt5/Qt5Config.cmake
	HINTS ${QT5_MSVC_DIR}
	PATHS C:/Qt/Qt5.6.3/5.6.3/msvc2015_64
	DOC "Location of the Qt5/msvc2015_64 directory"
	NO_DEFAULT_PATH)
set(QtConfig_INCLUDE_DIRS ${QtConfig_DIR}/include)

if (${QtConfig_DIR} STREQUAL "QtConfig_DIR-NOTFOUND")
	message(FATAL_ERROR "I need to know where the Qt/msvc2015_64 lives, try setting QT5_MSVC_DIR")
endif()

set(Qt5_DIR ${QtConfig_DIR}/lib/cmake/Qt5)
find_package(Qt5 COMPONENTS Widgets Core Gui REQUIRED)

set(SOURCES
	AnalysisResult.hpp
	Arm.hpp
	Backlog.hpp
	BlockPermutationEvaluator.hpp
	Broker.hpp
	common.hpp
	Condition.hpp
	ControlDialog.hpp
	DFGDisplay.hpp
	DFGNode.hpp
	DFGraph.hpp
	FunctionList.hpp
	PathOracle.hpp
	Predicate.hpp
	Processor.hpp
	ReferenceCount.hpp
	SignatureEvaluator.hpp
	SignatureParser.hpp
	SlidingStackedWidget.hpp
	ThreadPool.hpp
	types.hpp
	Arm.cpp
	Backlog.cpp
	BlockPermutationEvaluator.cpp
	Broker.cpp
	Condition.cpp
	ControlDialog.cpp
	DFGDisplay.cpp
	DFGNode.cpp
	DFGraph.cpp
	FunctionList.cpp
	PathOracle.cpp
	Plugin.cpp
	Predicate.cpp
	SignatureEvaluator.cpp
	SignatureParser.cpp
	SlidingStackedWidget.cpp
	ThreadPool.cpp
	Resource.qrc
)

# No need to compile with W3 (default)
if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
	string(REGEX REPLACE "/W[0-4]" "/W1" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W1")
endif()

add_library(${PROJECT_NAME} MODULE ${SOURCES})
target_compile_definitions(${PROJECT_NAME} PUBLIC
	__NT__
	QT_DLL
	QT_GUI_LIB
	QT_CORE_LIB
	QT_THREAD_SUPPORT
	QT_NAMESPACE=QT
)
target_include_directories(${PROJECT_NAME} PUBLIC ${IdaSdk_INCLUDE_DIRS} ${QtConfig_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME}
	${IdaSdk_DIR}/lib/x64_win_vc_32/ida.lib
	Qt5Widgets.lib
	Qt5Gui.lib
	Qt5Core.lib
)
target_link_directories(${PROJECT_NAME} PUBLIC ${IdaSdk_DIR}/lib/x64_win_qt)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 11)
