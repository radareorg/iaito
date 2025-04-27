// ShortcutKeysDialog.h
#ifndef SHORTCUTKEYSDIALOG_H
#define SHORTCUTKEYSDIALOG_H

#include <QDialog>
#include "common/ShortcutKeys.h"
#include "core/Iaito.h"
#include <QKeyEvent>
#include <QEvent>

class QTableWidget;
class QPushButton;

/**
 * @brief Dialog for setting or jumping to shortcut marks.
 */
class ShortcutKeysDialog : public QDialog {
    Q_OBJECT
public:
    enum Mode { SetMark, JumpTo };

    /**
     * @brief Constructor.
     * @param mode SetMark for saving, JumpTo for seeking.
     * @param currentAddr current address (used in SetMark).
     */
    ShortcutKeysDialog(Mode mode, RVA currentAddr = RVA_INVALID, QWidget* parent = nullptr);

    /**
     * @brief selectedKey returns the key chosen.
     */
    QChar selectedKey() const;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    /**
     * @brief Intercept key events on the table to allow keyboard-only selection.
     */
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onTableDoubleClicked(int row, int column);
    void onDelete();

private:
    void refreshTable();
    Mode m_mode;
    RVA m_currentAddr;
    QChar m_selectedKey;
    QTableWidget* m_table = nullptr;
    QPushButton* m_deleteButton = nullptr;
};

#endif // SHORTCUTKEYSDIALOG_H