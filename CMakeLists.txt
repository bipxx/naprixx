cmake_minimum_required(VERSION 3.18...4.4)
project(naparixx VERSION 0.3.0 LANGUAGES CXX)

add_library(naparixx_core STATIC cpp/application.cpp cpp/layer_core.cpp cpp/slicing.cpp cpp/change_journal.cpp cpp/collection_core.cpp)
target_compile_features(naparixx_core PUBLIC cxx_std_17)
target_include_directories(naparixx_core PUBLIC cpp/include)
set_target_properties(naparixx_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(naparixx_core PRIVATE /W4 /permissive- /fp:precise)
else()
  target_compile_options(naparixx_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(NAPARIXX_BUILD_CORE_TESTS "Build the standalone C++ application tests" OFF)
if(NAPARIXX_BUILD_CORE_TESTS)
  enable_testing()
  add_executable(naparixx_core_tests cpp/tests/application_tests.cpp)
  target_link_libraries(naparixx_core_tests PRIVATE naparixx_core)
  add_test(NAME application_core COMMAND naparixx_core_tests)
  add_executable(naparixx_change_tests cpp/tests/change_journal_tests.cpp)
  target_link_libraries(naparixx_change_tests PRIVATE naparixx_core)
  add_test(NAME change_journal COMMAND naparixx_change_tests)
  add_executable(naparixx_collection_tests cpp/tests/collection_tests.cpp)
  target_link_libraries(naparixx_collection_tests PRIVATE naparixx_core)
  add_test(NAME collection_core COMMAND naparixx_collection_tests)
endif()

option(NAPARIXX_BUILD_QT "Build the native Qt6 interoperability library" OFF)
option(NAPARIXX_BUILD_QT5 "Build the native Qt5 interoperability library" OFF)
set(_nx_qt_versions)
if(NAPARIXX_BUILD_QT5)
  find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets)
  list(APPEND _nx_qt_versions 5)
endif()
if(NAPARIXX_BUILD_QT)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
  list(APPEND _nx_qt_versions 6)
endif()
foreach(_nx_qt_version IN LISTS _nx_qt_versions)
  if(_nx_qt_version EQUAL 5)
    set(_nx_qt_target naparixx_qt5)
    set(_nx_qt_test_name qt5_widget_host)
  else()
    set(_nx_qt_target naparixx_qt)
    set(_nx_qt_test_name qt_widget_host)
  endif()
  add_library(${_nx_qt_target} STATIC cpp/qt_host.cpp)
  target_compile_features(${_nx_qt_target} PUBLIC cxx_std_17)
  target_include_directories(${_nx_qt_target} PUBLIC cpp/include)
  # Qt's slots macro conflicts with CPython's PyType_Spec::slots member.
  target_compile_definitions(${_nx_qt_target} PUBLIC QT_NO_KEYWORDS)
  target_link_libraries(${_nx_qt_target} PUBLIC Qt${_nx_qt_version}::Widgets)
  set_target_properties(${_nx_qt_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(${_nx_qt_target} PRIVATE /W4 /permissive- /fp:precise)
  else()
    target_compile_options(${_nx_qt_target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  if(NAPARIXX_BUILD_CORE_TESTS)
    add_executable(${_nx_qt_target}_tests cpp/tests/qt_host_tests.cpp)
    target_link_libraries(${_nx_qt_target}_tests PRIVATE ${_nx_qt_target})
    if(WIN32)
      # Other programs can add incompatible Qt DLLs to PATH. Test the SDK
      # against which this executable was linked, including its QPA plugins.
      add_test(NAME ${_nx_qt_test_name} COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt${_nx_qt_version}::Core>;$ENV{PATH}"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt${_nx_qt_version}::Core>/../plugins"
        "QT_QPA_PLATFORM=offscreen" $<TARGET_FILE:${_nx_qt_target}_tests>)
    else()
      add_test(NAME ${_nx_qt_test_name} COMMAND ${_nx_qt_target}_tests)
    endif()
    set_tests_properties(${_nx_qt_test_name} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
  endif()
endforeach()

option(NAPARIXX_BUILD_PYTHON "Build the nanobind extension" ON)
if(NOT NAPARIXX_BUILD_PYTHON)
  return()
endif()

find_package(Python 3.11 REQUIRED COMPONENTS Interpreter Development.Module)
execute_process(
  COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
  OUTPUT_VARIABLE nanobind_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE nanobind_result)
if(NOT nanobind_result EQUAL 0)
  message(FATAL_ERROR "Unable to locate nanobind for ${Python_EXECUTABLE}")
endif()
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_native NOMINSIZE
  cpp/module.cpp
  cpp/transform_state_owner.cpp
  cpp/transforms.cpp
  cpp/transform_policy.cpp
  cpp/transform_objects.cpp
  cpp/transform_backend.cpp
  cpp/collection_bindings.cpp
  cpp/nested_collection_bindings.cpp
  cpp/selection_bindings.cpp
  cpp/dictionary_bindings.cpp
  cpp/points.cpp
  cpp/labels.cpp
  cpp/labels_display.cpp
  cpp/labels_response_display.cpp
  cpp/viewer_close.cpp
  cpp/viewer_status_workers.cpp
  cpp/viewer_status_providers.cpp
  cpp/viewer_status_geometry.cpp
  cpp/viewer_status_format.cpp
  cpp/viewer_shape_state.cpp
  cpp/viewer_shape_model.cpp
  cpp/viewer_dims_reconciliation.cpp
  cpp/viewer_reset_fit.cpp
  cpp/viewer_layer_lifecycle.cpp
  cpp/settings_grid_publication.cpp
  cpp/theme_color_publication.cpp
  cpp/theme_registration_icons.cpp
  cpp/theme_state.cpp
  cpp/shape_geometry.cpp
  cpp/shape_publication.cpp
  cpp/shape_acceleration.cpp
  cpp/viewer_animation_workers.cpp
  cpp/label_colormap.cpp
  cpp/colormap_acceleration.cpp
  cpp/colormap_inner_loops.cpp
  cpp/direct_colormap_cache.cpp
  cpp/label_colormap_map.cpp
  cpp/vispy_labels.cpp
  cpp/vispy_scalar_field.cpp
  cpp/vispy_texture_services.cpp
  cpp/vispy_controller_state.cpp
  cpp/vispy_base_layer.cpp
  cpp/vispy_image_visuals.cpp
  cpp/vispy_image_layer.cpp
  cpp/layer_publication.cpp
  cpp/layer_thumbnails.cpp
  cpp/intensity_validation.cpp
  cpp/layer_construction.cpp
  cpp/image_construction.cpp
  cpp/layer_first_refresh.cpp
  cpp/scalar_first_refresh.cpp
  cpp/dask_indexing.cpp
  cpp/layer_data_geometry.cpp
  cpp/scalar_shape_extent.cpp
  cpp/slice_dataclass_state.cpp
  cpp/scalar_slice_executor.cpp
  cpp/multiscale_data.cpp
  cpp/scalar_viewport_policy.cpp
  cpp/scalar_data_controller.cpp
  cpp/geometry.cpp
  cpp/range.cpp
  cpp/application_bindings.cpp
  cpp/dims_model_bindings.cpp
  cpp/dims_namespace_owner.cpp
  cpp/dims_dictionary.cpp
  cpp/dims_flags_bindings.cpp
  cpp/layer_bindings.cpp
  cpp/slicing_bindings.cpp
  cpp/layer_slice_scheduler.cpp
  cpp/orchestration.cpp
  cpp/model_bindings.cpp
  cpp/model_helpers.cpp
  cpp/model_lifecycle.cpp
  cpp/model_transaction.cpp
  cpp/component_state.cpp
  cpp/grid_controller.cpp
  cpp/layerlist_controller.cpp
  cpp/qt_lifecycle.cpp
  cpp/qt_focus.cpp
  cpp/qt_focus_connection.cpp
  cpp/annotation_bindings.cpp
  cpp/model_metadata.cpp
  cpp/event_objects.cpp
  cpp/notifications.cpp
  cpp/event_state_owner.cpp
  cpp/event_subscriptions.cpp
  cpp/event_delivery.cpp
  cpp/event_groups.cpp
  cpp/event_tracing.cpp
  cpp/data_bindings.cpp)
target_compile_features(_native PRIVATE cxx_std_17)
target_include_directories(_native PRIVATE cpp/include)
target_link_libraries(_native PRIVATE naparixx_core)
if(MSVC)
  target_compile_options(_native PRIVATE /W4 /permissive- /fp:precise)
else()
  target_compile_options(_native PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS _native LIBRARY DESTINATION naparixx RUNTIME DESTINATION naparixx)

foreach(_nx_qt_version IN LISTS _nx_qt_versions)
  if(_nx_qt_version EQUAL 5)
    set(_nx_qt_module _qt5_native)
    set(_nx_qt_target naparixx_qt5)
  else()
    set(_nx_qt_module _qt_native)
    set(_nx_qt_target naparixx_qt)
  endif()
  nanobind_add_module(${_nx_qt_module} NOMINSIZE cpp/qt_host_bindings.cpp)
  target_compile_features(${_nx_qt_module} PRIVATE cxx_std_17)
  target_compile_definitions(${_nx_qt_module} PRIVATE NAPARIXX_QT_MODULE=${_nx_qt_module})
  target_link_libraries(${_nx_qt_module} PRIVATE ${_nx_qt_target})
  if(MSVC)
    target_compile_options(${_nx_qt_module} PRIVATE /W4 /permissive- /fp:precise)
  else()
    target_compile_options(${_nx_qt_module} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  install(TARGETS ${_nx_qt_module} LIBRARY DESTINATION naparixx RUNTIME DESTINATION naparixx)
endforeach()
