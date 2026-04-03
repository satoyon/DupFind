#include <QApplication>
#include <QSharedMemory>
#include "MainWindow.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // Create a shared memory object to indicate that the GUI is running
    QSharedMemory sharedMem("DupFind_GUI_Instance");
    if (!sharedMem.create(1)) {
        // If create fails, it might mean another GUI instance is running,
        // or a previous crash left it. But usually it's used as a flag.
        // We can just try to attach to see if it's really there.
        if (!sharedMem.attach()) {
            sharedMem.create(1);
        }
    }

    MainWindow window;
    window.resize(1024, 768);
    window.show();
    return app.exec();
}
