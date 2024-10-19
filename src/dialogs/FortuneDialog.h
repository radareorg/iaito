#ifndef FORTUNEDIALOG_H
#define FORTUNEDIALOG_H

#include <QDialog>
#include <QStringList>

class QLabel;
class QPushButton;

class FortuneDialog : public QDialog {
    Q_OBJECT

public:
    explicit FortuneDialog(QWidget *parent = nullptr);

private slots:
    void changeSentence();

private:
    QLabel *label;
    QPushButton *button;
};

#endif // FORTUNEDIALOG_H
