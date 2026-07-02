cmake_minimum_required(VERSION 3.19)
project(orderbook-visualizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC Debug builds trigger a Qt 6 assertion in the TLS backend static initializer
# (fires before QApplication is created due to DLL load ordering). RelWithDebInfo
# uses the release runtime which skips these assertions while keeping debug symbols.
if(MSVC AND (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets WebSockets Charts Network Test)

# Suppress MSVC debug CRT assertion dialogs for Qt static-init ordering warnings.
# These are benign in Qt 6 MSVC Debug builds (TLS backend fires before QApplication).
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # Use /MD (release CRT) even in debug to avoid Qt Debug DLL mismatch issues
    # Leave default: Qt's CMake integration sets this correctly per build type
endif()

qt_standard_project_setup()

# ── WebView2 (Microsoft Edge embedded browser) ────────────────────────────────
set(WEBVIEW2_DIR "${CMAKE_SOURCE_DIR}/third_party/webview2")

qt_add_executable(orderbook-visualizer
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    src/network/IExchangeClient.h
    src/network/WebSocketClient.h
    src/network/WebSocketClient.cpp
    src/network/BybitClient.h
    src/network/BybitClient.cpp
    src/network/BinanceClient.h
    src/network/BinanceClient.cpp
    src/network/CS2MarketClient.h
    src/network/CS2MarketClient.cpp
    src/network/OpenSkinClient.h
    src/network/OpenSkinClient.cpp
    src/util/CandleBuilder.h
    src/util/CandleBuilder.cpp
    src/util/ShimmerTimer.h
    src/util/ShimmerTimer.cpp
    src/core/Types.h
    src/orderbook/OrderBook.h
    src/orderbook/OrderBook.cpp
    src/model/Analytics.h
    src/model/Analytics.cpp
    src/model/MarketDataModel.h
    src/model/MarketDataModel.cpp
    src/ui/HeatmapWidget.h
    src/ui/HeatmapWidget.cpp
    src/ui/AnalyticsPanel.h
    src/ui/AnalyticsPanel.cpp
    src/ui/TapeWidget.h
    src/ui/TapeWidget.cpp
    src/ui/OrderBookLadder.h
    src/ui/OrderBookLadder.cpp
    src/ui/DepthChartWidget.h
    src/ui/DepthChartWidget.cpp
    src/ui/CandlestickWidget.h
    src/ui/CandlestickWidget.cpp
    src/ui/VolumeProfileWidget.h
    src/ui/VolumeProfileWidget.cpp
    src/ui/DeltaBarWidget.h
    src/ui/DeltaBarWidget.cpp
    src/model/CS2Types.h
    src/ui/CS2PriceChartWidget.h
    src/ui/CS2PriceChartWidget.cpp
    src/ui/CS2MarketStatsWidget.h
    src/ui/CS2MarketStatsWidget.cpp
    src/ui/CS2MarketplaceWidget.h
    src/ui/CS2MarketplaceWidget.cpp
)

target_include_directories(orderbook-visualizer PRIVATE
    src
    ${WEBVIEW2_DIR}/include
)

target_link_libraries(orderbook-visualizer
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::WebSockets
        Qt6::Charts
        Qt6::Network
        "${WEBVIEW2_DIR}/lib/WebView2Loader.dll.lib"
)

# Deploy Qt TLS backend plugins (windeployqt misses these on MSVC)
if(MSVC)
    set(QT_TLS_PLUGIN_DIR "${CMAKE_PREFIX_PATH}/plugins/tls")
    add_custom_command(TARGET orderbook-visualizer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:orderbook-visualizer>/tls"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_TLS_PLUGIN_DIR}/qschannelbackend.dll"
            "$<TARGET_FILE_DIR:orderbook-visualizer>/tls/qschannelbackend.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_TLS_PLUGIN_DIR}/qcertonlybackend.dll"
            "$<TARGET_FILE_DIR:orderbook-visualizer>/tls/qcertonlybackend.dll"
        COMMENT "Copying Qt TLS backend plugins"
    )
endif()

# Copy WebView2Loader.dll next to the executable at build time
add_custom_command(TARGET orderbook-visualizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WEBVIEW2_DIR}/lib/WebView2Loader.dll"
        "$<TARGET_FILE_DIR:orderbook-visualizer>/WebView2Loader.dll"
    COMMENT "Copying WebView2Loader.dll to output directory"
)

# ── Unit tests ────────────────────────────────────────────────────────────────
enable_testing()

qt_add_executable(test_orderbook tests/test_orderbook.cpp src/orderbook/OrderBook.cpp)
target_include_directories(test_orderbook PRIVATE src)
target_link_libraries(test_orderbook PRIVATE Qt6::Core Qt6::Test)
add_test(NAME OrderBook COMMAND test_orderbook)

qt_add_executable(test_analytics
    tests/test_analytics.cpp
    src/model/Analytics.cpp
    src/orderbook/OrderBook.cpp)
target_include_directories(test_analytics PRIVATE src)
target_link_libraries(test_analytics PRIVATE Qt6::Core Qt6::Test)
add_test(NAME Analytics COMMAND test_analytics)

qt_add_executable(test_cs2candles
    tests/test_cs2candles.cpp
    src/util/CandleBuilder.cpp
    src/orderbook/OrderBook.cpp)
target_include_directories(test_cs2candles PRIVATE src)
target_link_libraries(test_cs2candles PRIVATE Qt6::Core Qt6::Test)
add_test(NAME CS2Candles COMMAND test_cs2candles)

qt_add_executable(test_cs2skin
    tests/test_cs2skin.cpp
    src/network/OpenSkinClient.cpp)
target_include_directories(test_cs2skin PRIVATE src)
target_link_libraries(test_cs2skin PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME CS2Skin COMMAND test_cs2skin)

qt_add_executable(test_indicators tests/test_indicators.cpp)
target_include_directories(test_indicators PRIVATE src)
target_link_libraries(test_indicators PRIVATE Qt6::Core Qt6::Test)
add_test(NAME Indicators COMMAND test_indicators)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS orderbook-visualizer
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET orderbook-visualizer
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
