#ifndef INITIALIZATIONFILEEDITOR_H
#define INITIALIZATIONFILEEDITOR_H

#include "core/Iaito.h"
#include <memory>
#include <QDialog>
#include <QPushButton>

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

#endif // INITIALIZATIONFILEEDITOR_H
