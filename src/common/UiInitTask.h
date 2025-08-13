 #ifndef UIINITTASK_H
 #define UIINITTASK_H

 #include "common/TaskManager.h"
 #include <QObject>

 class MainWindow;

 /**
  * Background task to initialize UI data (opcodes, seek, panels) after main window shows.
  */
 class UiInitTask : public Task {
     Q_OBJECT
     MainWindow *m_main;
 public:
     explicit UiInitTask(MainWindow *main);
     void run() override;
 };

 #endif // UIINITTASK_H