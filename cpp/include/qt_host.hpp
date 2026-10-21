#pragma once

// Qt application ownership only. Python wrapper ownership lives in adapters.
#include <QDockWidget>
#include <QMainWindow>
#include <QPointer>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace nx::qt {

// A Qt5 pointer must never be interpreted as an instance of a Qt6 class.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
inline namespace qt5 {
#else
inline namespace qt6 {
#endif

using DockToken = std::uint64_t;

class WidgetHost {
public:
    WidgetHost();
    ~WidgetHost();
    WidgetHost(const WidgetHost&) = delete;
    WidgetHost& operator=(const WidgetHost&) = delete;

    QMainWindow* window() const;
    DockToken add_dock(QWidget* widget, const std::string& title,
                       const std::string& area);
    QDockWidget* dock(DockToken token) const;
    QWidget* detach_dock(DockToken token);
    std::size_t dock_count() const;
    void set_canvas(QWidget* widget);
    QWidget* take_canvas();
    QWidget* canvas() const;
    void resize(int width, int height);
    void show();
    bool close();

private:
    struct DockRecord {
        QPointer<QDockWidget> dock;
        QPointer<QWidget> widget;
    };

    static void require_gui_thread();
    QMainWindow* require_window() const;
    void require_available_widget(QWidget* widget) const;
    QPointer<QMainWindow> window_;
    QPointer<QObject> application_;
    QThread* gui_thread_ = nullptr;
    QPointer<QWidget> canvas_;
    std::unordered_map<DockToken, DockRecord> docks_;
    DockToken next_token_ = 1;
    bool closing_ = false;
};

} // inline ABI namespace
} // namespace nx::qt
