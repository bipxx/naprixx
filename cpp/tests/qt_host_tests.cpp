#include "qt_host.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool missing_application = false;
        try { nx::qt::WidgetHost host; }
        catch (const std::runtime_error&) { missing_application = true; }
        require(missing_application, "host construction requires QApplication");
        QApplication application(argc, argv);
        for (int repetition = 0; repetition < 20; ++repetition) {
            nx::qt::WidgetHost host;
            QPointer<QMainWindow> window = host.window();
            QPointer<QPushButton> button = new QPushButton("plugin button");
            int clicks = 0;
            QObject::connect(button.data(), &QPushButton::clicked, [&clicks] { ++clicks; });
            const auto token = host.add_dock(button.data(), "plugin", "left");
            require(host.dock_count() == 1, "native dock registry");
            require(button->parentWidget() == host.dock(token), "native dock ownership");
            button->click();
            require(clicks == 1, "widget callbacks");
            require(host.detach_dock(token) == button, "detach preserves identity");
            require(!button->parentWidget(), "detached widget has no parent");
            require(host.dock_count() == 0, "detached dock is removed");
            delete button.data();
            QPointer<QLabel> canvas = new QLabel("canvas");
            host.set_canvas(canvas.data());
            require(host.canvas() == canvas, "native central widget ownership");
            require(host.take_canvas() == canvas, "canvas detach preserves identity");
            require(!canvas->parentWidget(), "canvas detached");
            host.set_canvas(canvas.data());
            bool wrong_thread = false;
            std::thread worker([&] {
                try { host.resize(100, 100); }
                catch (const std::runtime_error&) { wrong_thread = true; }
            });
            worker.join();
            require(wrong_thread, "cross-thread GUI mutation rejected");
            require(host.close(), "native host close");
            require(!window && !canvas, "close destroys native window and canvas");
            require(host.close(), "close is idempotent");
        }
        QPointer<QMainWindow> deferred;
        auto* host = new nx::qt::WidgetHost();
        deferred = host->window();
        std::thread worker([&] { delete host; });
        worker.join();
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        require(!deferred, "worker destructor schedules GUI-thread destruction");
        std::cout << "Native Qt ownership and thread contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
