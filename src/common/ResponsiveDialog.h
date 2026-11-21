#ifndef RESPONSIVEDIALOG_H
#define RESPONSIVEDIALOG_H

#include <QDialog>

class QScrollArea;

// Screen fitting is also installed application-wide for ordinary iaito dialogs.
class ResponsiveDialog : public QDialog
{
public:
    explicit ResponsiveDialog(QWidget *parent = nullptr);

    static void installApplicationHandler();
    // Present startup flows as pages of the mobile window, respecting its safe area.
    // Returns false on desktop, leaving the dialog unchanged.
    static bool prepareMobilePage(QDialog *dialog);
    // Keep the supplied actions outside the scrolling body. Existing widgets,
    // layouts, signal connections and tab order are retained.
    static QScrollArea *makeScrollable(QWidget *page, QWidget *actions = nullptr);
};

#endif // RESPONSIVEDIALOG_H
