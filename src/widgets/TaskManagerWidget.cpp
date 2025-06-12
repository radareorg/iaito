#include "TaskManagerWidget.h"
#include "core/MainWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QProgressBar>
#include <QDateTime>
#include <QApplication>

TaskManagerWidget *TaskManagerWidget::instance = nullptr;

TaskManagerWidget *TaskManagerWidget::getInstance(MainWindow *main)
{
    if (!instance) {
        instance = new TaskManagerWidget(main);
    }
    return instance;
}

// TasksModel Implementation

TasksModel::TasksModel(QObject *parent) : QAbstractTableModel(parent)
{
    updateTasks();
    
    // Connect to TaskManager signals to keep model updated
    TaskManager *mgr = TaskManager::getInstance();
    connect(mgr, &TaskManager::taskAdded, this, &TasksModel::updateTasks);
    connect(mgr, &TaskManager::taskRemoved, this, &TasksModel::updateTasks);
    connect(mgr, &TaskManager::taskStateChanged, this, &TasksModel::updateTasks);
}

TasksModel::~TasksModel()
{
}

int TasksModel::rowCount(const QModelIndex &) const
{
    return taskIds.size();
}

int TasksModel::columnCount(const QModelIndex &) const
{
    return COLUMN_COUNT;
}

QVariant TasksModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        switch (section) {
        case TITLE:
            return tr("Task");
        case PRIORITY:
            return tr("Priority");
        case STATE:
            return tr("Status");
        case PROGRESS:
            return tr("Progress");
        case TIME:
            return tr("Time");
        default:
            return QVariant();
        }
    }
    return QVariant();
}

QVariant TasksModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= taskIds.size() || index.column() >= COLUMN_COUNT)
        return QVariant();

    const quint64 taskId = taskIds.at(index.row());
    if (!tasks.contains(taskId))
        return QVariant();

    const Task::Ptr task = tasks[taskId];
    
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case TITLE:
            return task->title();
        case PRIORITY:
            return priorityToString(task->priority());
        case STATE:
            return stateToString(task->state());
        case PROGRESS:
            return task->progress();
        case TIME: {
            qint64 elapsed = task->elapsedTime();
            if (elapsed <= 0) {
                return "0:00";
            }
            int seconds = (elapsed / 1000) % 60;
            int minutes = (elapsed / (1000 * 60)) % 60;
            int hours = (elapsed / (1000 * 60 * 60));
            if (hours > 0) {
                return QString("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
            } else {
                return QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar('0'));
            }
        }
        default:
            return QVariant();
        }
    } else if (role == Qt::ToolTipRole) {
        if (index.column() == TITLE && !task->description().isEmpty()) {
            return task->description();
        }
    } else if (role == Qt::TextAlignmentRole) {
        if (index.column() == PROGRESS || index.column() == TIME) {
            return Qt::AlignCenter;
        }
    } else if (role == Qt::ForegroundRole) {
        if (task->state() == TaskState::Failed) {
            return QColor(Qt::red);
        } else if (task->state() == TaskState::Cancelled) {
            return QColor(Qt::darkGray);
        }
    } else if (role == Qt::UserRole) {
        // Return the task ID for use by delegates, etc.
        return taskId;
    } else if (role == Qt::UserRole + 1) {
        // Return cancelability status
        return task->isCancellable();
    } else if (role == Qt::UserRole + 2) {
        // Return the task state as an integer for the delegate
        return static_cast<int>(task->state());
    }
    
    return QVariant();
}

void TasksModel::updateTasks()
{
    beginResetModel();
    
    tasks.clear();
    taskIds.clear();
    
    QList<Task::Ptr> allTasks = TaskManager::getInstance()->getAllTasks();
    for (const Task::Ptr &task : allTasks) {
        quint64 id = task->title().toULongLong();  // This is a simplification; in real code, track IDs properly
        tasks[id] = task;
        taskIds.append(id);
    }
    
    endResetModel();
}

QString TasksModel::priorityToString(TaskPriority priority) const
{
    switch (priority) {
    case TaskPriority::Critical:
        return tr("Critical");
    case TaskPriority::High:
        return tr("High");
    case TaskPriority::Normal:
        return tr("Normal");
    case TaskPriority::Low:
        return tr("Low");
    default:
        return tr("Unknown");
    }
}

QString TasksModel::stateToString(TaskState state) const
{
    switch (state) {
    case TaskState::Queued:
        return tr("Queued");
    case TaskState::Running:
        return tr("Running");
    case TaskState::Completed:
        return tr("Completed");
    case TaskState::Cancelled:
        return tr("Cancelled");
    case TaskState::Failed:
        return tr("Failed");
    default:
        return tr("Unknown");
    }
}

// TaskProgressDelegate Implementation

TaskProgressDelegate::TaskProgressDelegate(QObject *parent) : QStyledItemDelegate(parent)
{
}

void TaskProgressDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, 
                              const QModelIndex &index) const
{
    if (index.column() == TasksModel::PROGRESS) {
        int progress = index.data(Qt::DisplayRole).toInt();
        
        QStyleOptionProgressBar progressBarOption;
        progressBarOption.rect = option.rect;
        progressBarOption.minimum = 0;
        progressBarOption.maximum = 100;
        progressBarOption.progress = progress;
        progressBarOption.text = QString("%1%").arg(progress);
        progressBarOption.textVisible = true;
        
        // Set colors based on task state
        TaskState state = static_cast<TaskState>(index.data(Qt::UserRole + 2).toInt());
        
        if (state == TaskState::Running) {
            progressBarOption.palette.setColor(QPalette::Highlight, QColor(0, 122, 204));
        } else if (state == TaskState::Completed) {
            progressBarOption.palette.setColor(QPalette::Highlight, QColor(0, 160, 0));
        } else if (state == TaskState::Failed) {
            progressBarOption.palette.setColor(QPalette::Highlight, QColor(200, 0, 0));
        } else if (state == TaskState::Cancelled) {
            progressBarOption.palette.setColor(QPalette::Highlight, QColor(100, 100, 100));
        }
        
        QApplication::style()->drawControl(QStyle::CE_ProgressBar, &progressBarOption, painter);
    } else {
        QStyledItemDelegate::paint(painter, option, index);
    }
}

// TaskManagerWidget Implementation

TaskManagerWidget::TaskManagerWidget(MainWindow *main) : IaitoDockWidget(main)
{
    setObjectName("TaskManagerWidget");
    setWindowTitle(tr("Task Manager"));
    
    setupUI();
    setupConnections();
    
    // Update the task list every second
    updateTimer.setInterval(1000);
    connect(&updateTimer, &QTimer::timeout, this, [this]() {
        model->updateTasks();
        updateStatus();
    });
    updateTimer.start();
}

TaskManagerWidget::~TaskManagerWidget()
{
    instance = nullptr;
}

void TaskManagerWidget::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    
    // Create task table
    tasksTable = new QTableView(this);
    tasksTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tasksTable->setSelectionMode(QAbstractItemView::SingleSelection);
    tasksTable->setAlternatingRowColors(true);
    tasksTable->setSortingEnabled(true);
    
    model = new TasksModel(this);
    tasksTable->setModel(model);
    
    tasksTable->setItemDelegateForColumn(TasksModel::PROGRESS, new TaskProgressDelegate(this));
    
    // Adjust column sizes
    tasksTable->horizontalHeader()->setSectionResizeMode(TasksModel::TITLE, QHeaderView::Stretch);
    tasksTable->horizontalHeader()->setSectionResizeMode(TasksModel::PRIORITY, QHeaderView::ResizeToContents);
    tasksTable->horizontalHeader()->setSectionResizeMode(TasksModel::STATE, QHeaderView::ResizeToContents);
    tasksTable->horizontalHeader()->setSectionResizeMode(TasksModel::PROGRESS, QHeaderView::Fixed);
    tasksTable->horizontalHeader()->setSectionResizeMode(TasksModel::TIME, QHeaderView::ResizeToContents);
    tasksTable->setColumnWidth(TasksModel::PROGRESS, 150);
    
    layout->addWidget(tasksTable);
    
    // Add button row
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    
    statusLabel = new QLabel(this);
    buttonLayout->addWidget(statusLabel, 1);
    
    cancelButton = new QPushButton(tr("Cancel"), this);
    cancelButton->setEnabled(false);
    buttonLayout->addWidget(cancelButton);
    
    clearButton = new QPushButton(tr("Clear Completed"), this);
    buttonLayout->addWidget(clearButton);
    
    layout->addLayout(buttonLayout);
    
    updateStatus();
}

void TaskManagerWidget::setupConnections()
{
    connect(tasksTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &TaskManagerWidget::onTaskSelectionChanged);
    
    connect(cancelButton, &QPushButton::clicked, 
            this, &TaskManagerWidget::onCancelTaskClicked);
    
    connect(clearButton, &QPushButton::clicked,
            this, &TaskManagerWidget::onClearCompletedClicked);
}

void TaskManagerWidget::updateStatus()
{
    QList<Task::Ptr> running = TaskManager::getInstance()->getRunningTasks();
    statusLabel->setText(tr("%1 tasks running").arg(running.size()));
}

void TaskManagerWidget::onTaskSelectionChanged()
{
    QModelIndexList selection = tasksTable->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        cancelButton->setEnabled(false);
        return;
    }
    
    QModelIndex index = selection.first();
    bool isCancellable = index.data(Qt::UserRole + 1).toBool();
    TaskState state = static_cast<TaskState>(index.data(Qt::UserRole + 2).toInt());
    
    cancelButton->setEnabled(isCancellable && 
        (state == TaskState::Queued || state == TaskState::Running));
}

void TaskManagerWidget::onCancelTaskClicked()
{
    QModelIndexList selection = tasksTable->selectionModel()->selectedRows();
    if (selection.isEmpty())
        return;
    
    QModelIndex index = selection.first();
    quint64 taskId = index.data(Qt::UserRole).toULongLong();
    
    TaskManager::getInstance()->cancelTask(taskId);
    
    // Update immediately for better UX
    model->updateTasks();
    updateStatus();
}

void TaskManagerWidget::onClearCompletedClicked()
{
    // This is a placeholder. In a real implementation, we'd add a method
    // to TaskManager to clear completed tasks
    
    // For now, just refresh the view
    model->updateTasks();
    updateStatus();
}