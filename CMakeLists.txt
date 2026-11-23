cmake_minimum_required(VERSION 3.24)
project(7zipLX VERSION 0.5.2 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SEVENZIP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/7z2606-src" CACHE PATH "Pristine 7-Zip source tree")
set(UPSTREAM_DIR "${CMAKE_CURRENT_BINARY_DIR}/upstream")
set(SEVENZIP_BUILD_ASM OFF CACHE BOOL "Enable x86-64 assembly optimizations (requires asmc, jwasm or uasm)")
option(SEVENZIP_EXTERNAL_CODECS "Build and load the optional 7z.so external codec/format module" ON)

if(NOT EXISTS "${SEVENZIP_SOURCE_DIR}/CPP/7zip/UI/Common/LoadCodecs.h")
  message(FATAL_ERROR
    "7-Zip source not found at ${SEVENZIP_SOURCE_DIR}.\n"
    "Run ./scripts/fetch-7zip.sh, or configure with "
    "-DSEVENZIP_SOURCE_DIR=/path/to/7z2606-src.")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -DSEVENZIP_SOURCE_DIR=${SEVENZIP_SOURCE_DIR}
    -DUPSTREAM_DIR=${UPSTREAM_DIR}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PrepareUpstream.cmake"
  RESULT_VARIABLE _prepare_result
)
if(NOT _prepare_result EQUAL 0)
  message(FATAL_ERROR "Preparing the upstream tree failed")
endif()

include(cmake/7zcore_sources.cmake)

set(_7z_asm_replaced_c
  C/7zCrcOpt.c
  C/XzCrc64Opt.c
  C/Sha1Opt.c
  C/Sha256Opt.c
  C/Sort.c
  C/AesOpt.c
  C/LzFindOpt.c
)

set(_7zasm_objects)
if(SEVENZIP_BUILD_ASM)
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    message(FATAL_ERROR "SEVENZIP_BUILD_ASM is only implemented for x86-64")
  endif()
  set(SEVENZIP_ASM_COMPILER "" CACHE FILEPATH
      "Assembler for 7-Zip's MASM-syntax sources (asmc, jwasm or uasm)")
  if(NOT SEVENZIP_ASM_COMPILER)
    find_program(_7z_asm_compiler NAMES asmc jwasm uasm)
    if(_7z_asm_compiler)
      set(SEVENZIP_ASM_COMPILER "${_7z_asm_compiler}" CACHE FILEPATH
          "Assembler for 7-Zip's MASM-syntax sources (asmc, jwasm or uasm)" FORCE)
    endif()
  endif()
  if(NOT SEVENZIP_ASM_COMPILER)
    message(FATAL_ERROR
      "SEVENZIP_BUILD_ASM requires asmc, jwasm or uasm.\n"
      "Install asmc (Debian/Ubuntu: asmc-linux) or set -DSEVENZIP_ASM_COMPILER=/path/to/asmc.")
  endif()
  set(_7zasm_sources
    7zCrcOpt.asm
    XzCrc64Opt.asm
    Sha1Opt.asm
    Sha256Opt.asm
    Sort.asm
    AesOpt.asm
    LzFindOpt.asm
    LzmaDecOpt.asm
  )
  get_filename_component(_7zasm_program "${SEVENZIP_ASM_COMPILER}" NAME_WE)
  if(_7zasm_program STREQUAL "jwasm")
    # JWasm does not implement the AES instructions used by AesOpt.asm.
    list(REMOVE_ITEM _7zasm_sources AesOpt.asm)
    list(REMOVE_ITEM _7z_asm_replaced_c C/AesOpt.c)
  endif()
  set(_7zasm_dir "${CMAKE_CURRENT_BINARY_DIR}/7zasm")
  file(MAKE_DIRECTORY "${_7zasm_dir}")
  set(_7zasm_deps)
  foreach(_name IN LISTS _7zasm_sources)
    get_filename_component(_stem "${_name}" NAME_WE)
    set(_obj "${_7zasm_dir}/${_stem}.o")
    add_custom_command(
      OUTPUT "${_obj}"
      COMMAND "${SEVENZIP_ASM_COMPILER}" -nologo -c -fpic -elf64
              -DABI_LINUX -I"${UPSTREAM_DIR}/Asm/x86"
              "-Fo${_obj}" "${UPSTREAM_DIR}/Asm/x86/${_name}"
      DEPENDS "${UPSTREAM_DIR}/Asm/x86/${_name}" "${UPSTREAM_DIR}/Asm/x86/7zAsm.asm"
              "${SEVENZIP_ASM_COMPILER}"
      WORKING_DIRECTORY "${UPSTREAM_DIR}/CPP/7zip/Bundles/Format7zF"
      COMMENT "Assembling ${_name}"
      VERBATIM)
    list(APPEND _7zasm_objects "${_obj}")
    list(APPEND _7zasm_deps "${_obj}")
  endforeach()
  add_custom_target(7zasm DEPENDS ${_7zasm_deps})
endif()

set(_7zcore_paths)
foreach(_src IN LISTS SEVENZIP_CORE_SOURCES)
  if(SEVENZIP_BUILD_ASM AND _src IN_LIST _7z_asm_replaced_c)
    continue()
  endif()
  list(APPEND _7zcore_paths "${UPSTREAM_DIR}/${_src}")
endforeach()

add_library(7zcore SHARED
  ${_7zcore_paths}
  ${_7zasm_objects}
  "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/MyInitGuid.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/WorkDirSettingsPosix.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/FolderSortPosix.cpp"
  "${UPSTREAM_DIR}/CPP/Windows/DLL.cpp"
)
target_include_directories(7zcore PUBLIC
  "${UPSTREAM_DIR}/C"
  "${UPSTREAM_DIR}/CPP"
  "${UPSTREAM_DIR}/CPP/7zip"
)
target_compile_definitions(7zcore PUBLIC
  _FILE_OFFSET_BITS=64
  _LARGEFILE_SOURCE
  _REENTRANT
)
if(SEVENZIP_EXTERNAL_CODECS)
  target_compile_definitions(7zcore PUBLIC Z7_EXTERNAL_CODECS)
endif()
if(SEVENZIP_BUILD_ASM)
  target_compile_definitions(7zcore PRIVATE Z7_LZMA_DEC_OPT)
  add_dependencies(7zcore 7zasm)
endif()
target_compile_options(7zcore PRIVATE
  -fPIC
  -Wno-unused-parameter
  -Wno-unused-variable
  -Wno-sign-compare
  -Wno-missing-field-initializers
)
find_package(Threads REQUIRED)
target_link_libraries(7zcore PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

if(SEVENZIP_EXTERNAL_CODECS)
  find_program(SEVENZIP_MAKE_PROGRAM NAMES make gmake)
  if(NOT SEVENZIP_MAKE_PROGRAM)
    message(FATAL_ERROR "SEVENZIP_EXTERNAL_CODECS=ON requires make to build 7z.so")
  endif()
  set(SEVENZIP_PLUGIN "${CMAKE_BINARY_DIR}/7z.so")
  set(SEVENZIP_PLUGIN_OBJDIR "${CMAKE_BINARY_DIR}/7zplugin")
  add_custom_command(
    OUTPUT "${SEVENZIP_PLUGIN}"
    COMMAND "${SEVENZIP_MAKE_PROGRAM}" -f makefile.gcc -j
            "O=${SEVENZIP_PLUGIN_OBJDIR}"
            "LDFLAGS_STATIC=-Wl,-Bsymbolic"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SEVENZIP_PLUGIN_OBJDIR}/7z.so" "${SEVENZIP_PLUGIN}"
    WORKING_DIRECTORY "${UPSTREAM_DIR}/CPP/7zip/Bundles/Format7zF"
    COMMENT "Building optional external 7z.so codec/format module"
    VERBATIM
  )
  add_custom_target(7zplugin ALL DEPENDS "${SEVENZIP_PLUGIN}")
endif()

find_package(Qt6 6.4 COMPONENTS Core Gui Widgets Concurrent REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_library(7ziplx_backend STATIC
  src/backend/ProgressState.cpp
  src/backend/PromptDispatcher.cpp
  src/backend/Unicode.cpp
  src/backend/PropVariantQt.cpp
  src/backend/SevenZipEngine.cpp
  src/backend/ArchiveSession.cpp
  src/backend/CompressOperation.cpp
  src/backend/LocalFileOperation.cpp
  src/backend/ArchiveWriteBack.cpp
  src/backend/MessageDialog.cpp
  src/backend/InputDialog.cpp
  src/backend/HashOperation.cpp
  src/backend/BenchmarkOperation.cpp
)
target_include_directories(7ziplx_backend PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_definitions(7ziplx_backend PRIVATE
  _FILE_OFFSET_BITS=64
  _LARGEFILE_SOURCE
  _REENTRANT
)
target_link_libraries(7ziplx_backend PUBLIC
  7zcore
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
)

add_library(7ziplx_ui STATIC
  resources/app.qrc
  src/app/MainWindow.cpp
  src/app/OperationRunner.cpp
  src/app/ExternalToolController.cpp
  src/app/HashController.cpp
  src/app/TransferController.cpp
  src/models/LocalFsModel.cpp
  src/models/RootModel.cpp
  src/models/ArchiveFolderModel.cpp
  src/ui/Theme.cpp
  src/ui/dialogs/ProgressDialog.cpp
  src/ui/dialogs/ExtractDialog.cpp
  src/ui/dialogs/CompressDialog.cpp
  src/ui/dialogs/HashResultDialog.cpp
  src/ui/dialogs/OptionsDialog.cpp
  src/ui/dialogs/AboutDialog.cpp
  src/ui/dialogs/BenchmarkDialog.cpp
  src/ui/widgets/FilePanel.cpp
  src/ui/widgets/FileView.cpp
  src/ui/widgets/PanelTransfer.cpp
)
target_link_libraries(7ziplx_ui PUBLIC 7ziplx_backend Qt6::Widgets Qt6::Concurrent)

add_executable(7ziplx src/main.cpp)
target_link_libraries(7ziplx PRIVATE 7ziplx_ui)
if(SEVENZIP_EXTERNAL_CODECS)
  add_dependencies(7ziplx 7zplugin)
endif()

option(BUILD_SCREENSHOTS "Build the README screenshot generator" OFF)
if(BUILD_SCREENSHOTS)
  add_executable(7ziplx-screenshots tools/screenshot_app.cpp)
  target_compile_definitions(7ziplx-screenshots PRIVATE
    SCREENSHOT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(7ziplx-screenshots PRIVATE 7ziplx_ui)
endif()

include(CTest)
if(BUILD_TESTING)
  find_package(Qt6 6.4 QUIET COMPONENTS Test)
  if(Qt6Test_FOUND)
    add_executable(7ziplx_tests tests/test_archive_operations.cpp)
    target_link_libraries(7ziplx_tests PRIVATE 7ziplx_backend Qt6::Test)
    add_test(NAME archive_operations COMMAND 7ziplx_tests)
    set_tests_properties(archive_operations PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1"
      TIMEOUT 300)
  else()
    message(STATUS "Qt6::Test not found; 7zipLX tests are disabled")
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS 7ziplx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS 7zcore LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(SEVENZIP_EXTERNAL_CODECS)
  install(FILES "${SEVENZIP_PLUGIN}" DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(FILES packaging/7ziplx.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/servicemenus/7ziplx-servicemenu.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kio/servicemenus)
install(FILES packaging/nemo-actions/7ziplx-extract-here.nemo_action
              packaging/nemo-actions/7ziplx-compress.nemo_action
        DESTINATION ${CMAKE_INSTALL_DATADIR}/nemo/actions)
install(PROGRAMS packaging/nautilus-scripts/Extract\ Here
                 packaging/nautilus-scripts/Compress\ to\ 7z
        DESTINATION ${CMAKE_INSTALL_DATADIR}/nautilus/scripts)
install(PROGRAMS scripts/install-file-manager-actions.sh
                 scripts/update-translations.sh
        DESTINATION ${CMAKE_INSTALL_DATADIR}/7ziplx)
install(FILES packaging/io.github.claudiujitea.7zipLX.appdata.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES NOTICE.md README.md docs/USER-GUIDE.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
# README links docs/screenshots/...; USER-GUIDE links screenshots/... Both are
# installed at the documentation root, so provide both relative paths.
install(DIRECTORY docs/screenshots/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs/screenshots)
install(DIRECTORY docs/screenshots/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/screenshots)
install(FILES packaging/7ziplx.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(DIRECTORY resources/translations/ DESTINATION ${CMAKE_INSTALL_DATADIR}/7ziplx/translations)
install(FILES LICENSES/7zip-License.txt DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME 7zip-License.txt)
install(FILES LICENSES/7zip-unRarLicense.txt DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME 7zip-unRarLicense.txt)
foreach(_size 16 24 32 48 64 128 256)
  install(FILES resources/icons/7ziplx-${_size}.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps
          RENAME 7ziplx.png)
endforeach()
set_target_properties(7ziplx PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
