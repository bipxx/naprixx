#include "qt_host.hpp"

#include <QApplication>
#include <QThread>
#include <limits>
#include <stdexcept>

namespace nx::qt {

void WidgetHost::require_gui_thread() {
    auto* application = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!application)
        throw std::runtime_error("a QApplication is required for the native Qt host");
    if (QThread::currentThread() != application->thread())
        throw std::runtime_error("native Qt widgets must be used on the GUI thread");
}

WidgetHost::WidgetHost() {
    require_gui_thread();
    application_ = QCoreApplication::instance();
    gui_thread_ = application_->thread();
    window_ = new QMainWindow();
    window_->setObjectName(QStringLiteral("naparixx.native.widget.host"));
    window_->setWindowTitle(QStringLiteral("naparixx native Qt interoperability"));
}

WidgetHost::~WidgetHost() {
    const auto window = window_;
    window_.clear();
    docks_.clear();
    canvas_.clear();
    if (!window) return;
    if (QThread::currentThread() == gui_thread_) {
        delete window.data();
    } else if (application_) {
        // Resolve the guarded widget on the GUI thread, where its destruction
        // is serialized with any external deleteLater() request.
        QMetaObject::invokeMethod(application_.data(), [window] {
            if (window) delete window.data();
        }, Qt::QueuedConnection);
    }
}

QMainWindow* WidgetHost::window() const {
    require_gui_thread();
    return window_.data();
}

QMainWindow* WidgetHost::require_window() const {
    require_gui_thread();
    if (!window_) throw std::runtime_error("the native Qt host is closed");
    return window_.data();
}

void WidgetHost::require_available_widget(QWidget* widget) const {
    auto* window = require_window();
    if (!widget) throw std::invalid_argument("a QWidget is required");
    if (widget->thread() != window->thread())
        throw std::invalid_argument("the widget belongs to another thread");
    if (widget == window || widget->isAncestorOf(window))
        throw std::invalid_argument("a host cannot contain itself or an ancestor");
    if (widget == canvas_)
        throw std::invalid_argument("the widget is already the canvas");
    for (const auto& item : docks_)
        if (item.second.widget == widget)
            throw std::invalid_argument("the widget is already docked");
    if (widget->parentWidget())
        throw std::invalid_argument("detach the widget from its current parent first");
}

DockToken WidgetHost::add_dock(QWidget* widget, const std::string& title,
                               const std::string& area) {
    require_available_widget(widget);
    Qt::DockWidgetArea location;
    if (area == "left") location = Qt::LeftDockWidgetArea;
    else if (area == "right") location = Qt::RightDockWidgetArea;
    else if (area == "top") location = Qt::TopDockWidgetArea;
    else if (area == "bottom") location = Qt::BottomDockWidgetArea;
    else throw std::invalid_argument("dock area must be left, right, top or bottom");
    if (next_token_ == std::numeric_limits<DockToken>::max())
        throw std::overflow_error("native dock tokens are exhausted");
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    using TextSize = int;
#else
    using TextSize = qsizetype;
#endif
    if (title.size() > static_cast<std::size_t>(std::numeric_limits<TextSize>::max()))
        throw std::overflow_error("dock title exceeds the Qt string size limit");
    const auto token = next_token_++;
    auto* dock = new QDockWidget(QString::fromUtf8(title.data(),
                                                 static_cast<TextSize>(title.size())),
                                  window_.data());
    dock->setObjectName(QStringLiteral("naparixx.plugin.") + QString::number(token));
    dock->setWidget(widget);
    window_->addDockWidget(location, dock);
    docks_.emplace(token, DockRecord{dock, widget});
    return token;
}

QDockWidget* WidgetHost::dock(DockToken token) const {
    require_window();
    return docks_.at(token).dock.data();
}

QWidget* WidgetHost::detach_dock(DockToken token) {
    auto* window = require_window();
    const auto record = docks_.at(token);
    docks_.erase(token);
    auto* widget = record.widget.data();
    if (widget) widget->setParent(nullptr);
    if (record.dock) {
        record.dock->setWidget(nullptr);
        window->removeDockWidget(record.dock.data());
        delete record.dock.data();
    }
    return widget;
}

std::size_t WidgetHost::dock_count() const {
    require_gui_thread();
    return docks_.size();
}

void WidgetHost::set_canvas(QWidget* widget) {
    require_available_widget(widget);
    if (canvas_) throw std::runtime_error("detach the existing canvas first");
    window_->setCentralWidget(widget);
    canvas_ = widget;
}

QWidget* WidgetHost::take_canvas() {
    auto* window = require_window();
    // A foreign ownership adapter may already have detached the Qt parent.
    auto* widget = canvas_.data();
    window->takeCentralWidget();
    canvas_.clear();
    if (widget) widget->setParent(nullptr);
    return widget;
}

QWidget* WidgetHost::canvas() const {
    require_gui_thread();
    return canvas_.data();
}

void WidgetHost::resize(int width, int height) {
    auto* window = require_window();
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("window dimensions must be positive");
    window->resize(width, height);
}

void WidgetHost::show() { require_window()->show(); }

bool WidgetHost::close() {
    require_gui_thread();
    auto* window = window_.data();
    if (!window) return true;
    // close() dispatches plugin event filters before destroying the window.
    // Reentrant calls must leave leases intact until that decision is final.
    if (closing_) return false;
    closing_ = true;
    if (!window->close()) {
        closing_ = false;
        return false;
    }
    // Clear before destruction: Python Qt callbacks may reenter close().
    window_.clear();
    docks_.clear();
    canvas_.clear();
    delete window;
    closing_ = false;
    return true;
}

} // namespace nx::qt
