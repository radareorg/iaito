#ifndef TASKMANAGERWIDGET_H
#define TASKMANAGERWIDGET_H

#include "IaitoDockWidget.h"
#include "common/TaskManager.h"

#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QPushButton>
#include <QLabel>

class QProgressBar;

class TasksModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        TITLE = 0,
        PRIORITY,
        STATE,
        PROGRESS,
        TIME,
        COLUMN_COUNT
    };

    TasksModel(QObject *parent = nullptr);
    ~TasksModel();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

public slots:
    void updateTasks();

private:
    QMap<quint64, Task::Ptr> tasks;
    QVector<quint64> taskIds;

    QString priorityToString(TaskPriority priority) const;
    QString stateToString(TaskState state) const;
};

class TaskProgressDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    TaskProgressDelegate(QObject *parent = nullptr);
    void paint(QPainter *painter, const QStyleOptionViewItem &option, 
              const QModelIndex &index) const override;
};

class MainWindow;

class TaskManagerWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit TaskManagerWidget(MainWindow *main);
    ~TaskManagerWidget();

    static TaskManagerWidget *getInstance(MainWindow *main);

private:
    static TaskManagerWidget *instance;
    QTableView *tasksTable;
    TasksModel *model;
    QPushButton *cancelButton;
    QPushButton *clearButton;
    QLabel *statusLabel;
    QTimer updateTimer;

    void setupUI();
    void setupConnections();
    void updateStatus();

private slots:
    void onTaskSelectionChanged();
    void onCancelTaskClicked();
    void onClearCompletedClicked();
};

#endif // TASKMANAGERWIDGET_H