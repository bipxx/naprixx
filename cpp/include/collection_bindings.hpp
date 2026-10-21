#pragma once
#include <nanobind/nanobind.h>
namespace nx { void bind_collections(nanobind::module_& module); }
namespace nx { void bind_nested_collections(nanobind::module_& module); }
namespace nx { void bind_selection(nanobind::module_& module); }
namespace nx { void bind_dictionaries(nanobind::module_& module); }
