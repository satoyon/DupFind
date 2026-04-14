#include "MainWindow.hpp"
#include <QApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>
#include <QSharedMemory>
#include <QTranslator>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  // Load translations
  QTranslator translator;

  // Priority 1: Use system locale name (respects LANG/LC_ALL environment
  // variables)
  QString systemLocale = QLocale::system().name();
#ifdef QT_DEBUG
  qDebug() << "Current system locale name:" << systemLocale;
#endif
  if (translator.load(":/i18n/dupfind_" + systemLocale)) {
#ifdef QT_DEBUG
    qDebug() << "Successfully loaded translation via system locale name:"
             << systemLocale;
#endif
    app.installTranslator(&translator);
  } else {
    // Priority 2: Fallback to uiLanguages list
    const QStringList uiLanguages = QLocale::system().uiLanguages();
#ifdef QT_DEBUG
    qDebug() << "System UI Languages (Fallback):" << uiLanguages;
#endif

    for (const QString &locale : uiLanguages) {
      const QString baseName = "dupfind_" + QLocale(locale).name();
      QString fullPath = ":/i18n/" + baseName;
      if (translator.load(fullPath)) {
#ifdef QT_DEBUG
        qDebug()
            << "Successfully loaded translation via UI Languages (Fallback):"
            << fullPath;
#endif
        app.installTranslator(&translator);
        break;
      }
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
