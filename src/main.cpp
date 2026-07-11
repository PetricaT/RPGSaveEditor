#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[])
{
    std::set_terminate([]() {
        try {
            std::exception_ptr eptr = std::current_exception();
            if (eptr) std::rethrow_exception(eptr);
        } catch (const std::exception& e) {
            std::cerr << "FATAL: Unhandled exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "FATAL: Unknown unhandled exception" << std::endl;
        }
        std::abort();
    });

    QApplication app(argc, argv);
    app.setApplicationName("RPGSaveEditor");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("RPGSaveEditor");
    app.setWindowIcon(QIcon(":/appicon.png"));

    try {
        MainWindow window;
        window.show();
        return app.exec();
    } catch (const std::exception& e) {
        std::cerr << "FATAL: " << e.what() << std::endl;
        return 1;
    }
}
