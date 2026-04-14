#include <QApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>
#include <QSharedMemory>
#include <QTranslator>
#include "MainWindow.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Load translations
    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    qDebug() << "System UI Languages:" << uiLanguages;

    for (const QString &locale : uiLanguages) {
        const QString baseName = "dupfind_" + QLocale(locale).name();
        QString fullPath = ":/i18n/" + baseName;
        qDebug() << "Attempting to load translation:" << fullPath;
        if (translator.load(fullPath)) {
            qDebug() << "Successfully loaded translation:" << fullPath;
            app.installTranslator(&translator);
            break;
        }
    }

    // Load standard Qt translations (for QFileDialog, etc.)
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale::system(), "qtbase", "_",
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtTranslator);
    }
    
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
