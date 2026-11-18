cmake_minimum_required(VERSION 3.22)

project(redalert_ios LANGUAGES C CXX OBJC)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "Configure the iOS target with -DCMAKE_SYSTEM_NAME=iOS, or use scripts/build_ios_debug.sh.")
endif()

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Xcode 27 rejects iOS deployment targets below 15.0, which makes every CMake
# try_compile (and therefore SDL's feature probes) fail. 15.0 builds with both
# Xcode 26 and Xcode 27.
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "" FORCE)
endif()

set(RA95_IOS_BUNDLE_ID "com.raport.redalert" CACHE STRING "Bundle identifier for the iOS debug app. Device builds need one your signing team can register.")

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

set(RA95_IOS_BUNDLED_ASSETS_DIR "${CMAKE_CURRENT_LIST_DIR}/../assets/redalert" CACHE PATH "Local Red Alert assets to bundle into the iOS debug app.")

# App icon. The repository ships an original design (ios/Assets.xcassets, drawn
# by ios/icon/make_app_icon.swift). A local 1024x1024 PNG can replace it, for
# example one made from the 1996 disc art by ios/icon/make_disc_art_icon.swift;
# game art stays in the git-ignored assets/ folder, never in the repository.
set(RA95_IOS_APP_ICON "" CACHE FILEPATH "Optional 1024x1024 PNG used as the app icon instead of the original design. Defaults to assets/AppIcon-1024.png when that file exists.")
set(RA95_IOS_EFFECTIVE_APP_ICON "${RA95_IOS_APP_ICON}")
if(NOT RA95_IOS_EFFECTIVE_APP_ICON AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../assets/AppIcon-1024.png")
    set(RA95_IOS_EFFECTIVE_APP_ICON "${CMAKE_CURRENT_LIST_DIR}/../assets/AppIcon-1024.png")
endif()
if(RA95_IOS_EFFECTIVE_APP_ICON)
    if(NOT EXISTS "${RA95_IOS_EFFECTIVE_APP_ICON}")
        message(FATAL_ERROR "RA95_IOS_APP_ICON does not exist: ${RA95_IOS_EFFECTIVE_APP_ICON}")
    endif()
    set(RA95_IOS_ASSET_CATALOG "${CMAKE_CURRENT_BINARY_DIR}/Assets.xcassets")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/Assets.xcassets/Contents.json" "${RA95_IOS_ASSET_CATALOG}/Contents.json" COPYONLY)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/Assets.xcassets/AppIcon.appiconset/Contents.json" "${RA95_IOS_ASSET_CATALOG}/AppIcon.appiconset/Contents.json" COPYONLY)
    configure_file("${RA95_IOS_EFFECTIVE_APP_ICON}" "${RA95_IOS_ASSET_CATALOG}/AppIcon.appiconset/AppIcon-1024.png" COPYONLY)
    message(STATUS "iOS app icon: ${RA95_IOS_EFFECTIVE_APP_ICON}")
else()
    set(RA95_IOS_ASSET_CATALOG "${CMAKE_CURRENT_LIST_DIR}/Assets.xcassets")
    message(STATUS "iOS app icon: original design in ios/Assets.xcassets")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/third_party/SDL/CMakeLists.txt")
    message(FATAL_ERROR "Missing SDL2 sources at ios/third_party/SDL. Run scripts/build_ios_debug.sh to fetch them.")
endif()

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/third_party/SDL" "${CMAKE_BINARY_DIR}/SDL2")

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/ra95_common.cmake")

add_executable(redalert_ios MACOSX_BUNDLE
    "${RA95_REPO_ROOT}/PORT/IOS/src/ios_app_delegate.m"
    "${RA95_REPO_ROOT}/PORT/IOS/src/ios_layout.m"
    "${RA95_IOS_ASSET_CATALOG}"
    "${RA95_REPO_ROOT}/PORT/IOS/src/ios_main.cpp"
    ${RA95_PLATFORM_SDL_SOURCES}
    ${RA95_SOURCES}
    ${RA95_SUPPORT_SOURCES}
)

ra95_configure_target(redalert_ios)

target_compile_definitions(redalert_ios PRIVATE
    RA_IOS
    RA_MOBILE_TOUCH
)

target_include_directories(redalert_ios PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/third_party/SDL/src/video/uikit"
)

if(TARGET SDL2::SDL2main)
    set(RA95_SDLMAIN_TARGET SDL2::SDL2main)
elseif(TARGET SDL2main)
    set(RA95_SDLMAIN_TARGET SDL2main)
else()
    message(FATAL_ERROR "SDL2main CMake target was not created")
endif()

if(TARGET SDL2::SDL2-static)
    set(RA95_SDL_TARGET SDL2::SDL2-static)
elseif(TARGET SDL2-static)
    set(RA95_SDL_TARGET SDL2-static)
elseif(TARGET SDL2::SDL2)
    set(RA95_SDL_TARGET SDL2::SDL2)
elseif(TARGET SDL2)
    set(RA95_SDL_TARGET SDL2)
else()
    message(FATAL_ERROR "SDL2 CMake target was not created")
endif()

target_link_libraries(redalert_ios PRIVATE
    ${RA95_SDLMAIN_TARGET}
    ${RA95_SDL_TARGET}
)

set_target_properties(redalert_ios PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${RA95_IOS_BUNDLE_ID}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    # App icon: the original design, or the local override chosen above.
    RESOURCE "${RA95_IOS_ASSET_CATALOG}"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
)

add_custom_command(TARGET redalert_ios POST_BUILD
    # Start from an empty folder so files deleted from assets/redalert (a language or
    # voice pack, say) also leave the bundle; the app then removes them from its copy.
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_BUNDLE_DIR:redalert_ios>/assets/redalert"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:redalert_ios>/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${RA95_IOS_BUNDLED_ASSETS_DIR}" "$<TARGET_BUNDLE_DIR:redalert_ios>/assets/redalert"
    COMMENT "Copying local Red Alert assets into the iOS debug bundle"
)
