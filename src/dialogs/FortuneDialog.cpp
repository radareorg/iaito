#include "FortuneDialog.h"
#include "core/Iaito.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

FortuneDialog::FortuneDialog(QWidget *parent)
    : QDialog(parent) {

    label = new QLabel(Core()->cmd("fo"), this);
    button = new QPushButton("Another Cookie", this);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(label);
    layout->addWidget(button);
    setLayout(layout);

    connect(button, &QPushButton::clicked, this, &FortuneDialog::changeSentence);

    setModal(true);
    adjustSize();
}

void FortuneDialog::changeSentence() {
    label->setText(Core()->cmd("fo"));
    adjustSize();
}
