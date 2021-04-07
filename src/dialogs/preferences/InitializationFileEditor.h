#ifndef INITIALIZATIONFILEEDITOR_H
#define INITIALIZATIONFILEEDITOR_H

#include <QDialog>
#include <QPushButton>
#include <memory>
#include "core/Iaito.h"

class PreferencesDialog;

namespace Ui {
class InitializationFileEditor;
}

class InitializationFileEditor : public QDialog
{
    Q_OBJECT

public:
    explicit InitializationFileEditor(PreferencesDialog *dialog);
    ~InitializationFileEditor();
    void saveIaitoRC();
    void executeIaitoRC();
    
private:
    std::unique_ptr<Ui::InitializationFileEditor> ui;

};


#endif //INITIALIZATIONFILEEDITOR_H
