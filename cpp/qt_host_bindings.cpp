#include "qt_host.hpp"

#include <QtGlobal>
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <stdexcept>

namespace nb = nanobind;
using nx::qt::WidgetHost;

namespace {
std::uintptr_t address(const QWidget* widget) {
    return reinterpret_cast<std::uintptr_t>(widget);
}

QWidget* widget(std::uintptr_t pointer) {
    if (!pointer) throw std::invalid_argument("a nonzero QWidget pointer is required");
    return reinterpret_cast<QWidget*>(pointer);
}
} // namespace

NB_MODULE(NAPARIXX_QT_MODULE, m) {
    m.doc() = "Native Qt ownership prototype; use naparixx.qt_host adapters.";
    m.attr("qt_build_version") = QT_VERSION_STR;
    m.attr("qt_build_major") = QT_VERSION >> 16;
    m.def("qt_runtime_version", [] { return std::string(qVersion()); });
    nb::class_<WidgetHost>(m, "WidgetHost", nb::is_weak_referenceable())
        .def(nb::init<>())
        .def_prop_ro("window_address", [](const WidgetHost& host) {
            return address(host.window());
        })
        .def("add_dock", [](WidgetHost& host, std::uintptr_t pointer,
                            const std::string& title, const std::string& area) {
            return host.add_dock(widget(pointer), title, area);
        }, nb::arg("widget_address"), nb::arg("title"), nb::arg("area") = "right")
        .def("dock_address", [](const WidgetHost& host, nx::qt::DockToken token) {
            return address(host.dock(token));
        })
        .def("detach_dock", [](WidgetHost& host, nx::qt::DockToken token) {
            return address(host.detach_dock(token));
        })
        .def_prop_ro("dock_count", &WidgetHost::dock_count)
        .def("set_canvas", [](WidgetHost& host, std::uintptr_t pointer) {
            host.set_canvas(widget(pointer));
        })
        .def("take_canvas", [](WidgetHost& host) { return address(host.take_canvas()); })
        .def_prop_ro("canvas_address", [](const WidgetHost& host) {
            return address(host.canvas());
        })
        .def("resize", &WidgetHost::resize)
        .def("show", &WidgetHost::show)
        .def("close", &WidgetHost::close);
}
