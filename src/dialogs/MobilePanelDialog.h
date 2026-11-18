#ifndef MOBILEPANELDIALOG_H
#define MOBILEPANELDIALOG_H

#include "common/ResponsiveDialog.h"

class MainWindow;

class MobilePanelDialog : public ResponsiveDialog
{
public:
    explicit MobilePanelDialog(MainWindow *main);
};

#endif
