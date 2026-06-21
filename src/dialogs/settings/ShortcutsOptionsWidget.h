#ifndef SHORTCUTSOPTIONSWIDGET_H
#define SHORTCUTSOPTIONSWIDGET_H

#include <QDialog>
#include <QHash>

class SettingsDialog;
class QTreeWidget;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QTableWidget;

class ShortcutsOptionsWidget : public QDialog
{
    Q_OBJECT
public:
    explicit ShortcutsOptionsWidget(SettingsDialog *parent);
    ~ShortcutsOptionsWidget() override;

private:
    void rebuild();
    void refreshConflicts();
    void applyFilter(const QString &text);

    void reloadCommandShortcuts();
    void addCommandRow();
    void saveCommandShortcuts();

    QLineEdit *filterEdit;
    QTreeWidget *tree;
    QLabel *conflictLabel;
    QHash<QString, QKeySequenceEdit *> editors;

    QTableWidget *commandTable;
    bool loadingCommands = false;
};

#endif // SHORTCUTSOPTIONSWIDGET_H
