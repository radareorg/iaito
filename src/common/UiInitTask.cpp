 #include "UiInitTask.h"
 #include "core/Iaito.h"
 #include "core/MainWindow.h"
 #include <QMetaObject>

 UiInitTask::UiInitTask(MainWindow *main)
     : Task(QObject::tr("UI Initialization"), TaskPriority::Low)
     , m_main(main)
 {
     setCancellable(false);
 }

 void UiInitTask::run() {
     // Load opcodes and update seek position
     Core()->getOpcodes();
     Core()->updateSeek();
     // Trigger all panel refreshes
     QMetaObject::invokeMethod(m_main, "refreshAll", Qt::QueuedConnection);
     // Show fortune message
     QString fortune = Core()->cmdRaw("fo");
     if (!fortune.isEmpty()) {
         Core()->message("\n" + fortune);
     }
 }