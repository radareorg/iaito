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
    // Keep the supplied actions outside the scrolling body. Existing widgets,
    // layouts, signal connections and tab order are retained.
    static QScrollArea *makeScrollable(QWidget *page, QWidget *actions = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
};

#endif // RESPONSIVEDIALOG_H
