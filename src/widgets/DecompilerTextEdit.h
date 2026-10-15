#ifndef DECOMPILERTEXTEDIT_H
#define DECOMPILERTEXTEDIT_H

#include <QMap>
#include <QPlainTextEdit>
#include <QVector>

class DecompilerTextEdit : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit DecompilerTextEdit(QWidget *parent = nullptr);

    void setDecompiledCode(const QString &text, const QString &source, bool preserveFolds);
    bool isFolded(int line) const;
    bool isFoldable(int line) const;

public slots:
    void toggleFold(int line);
    void toggleFoldAtCursor();
    void jumpToMatchingBrace();
    void collapseAll();
    void expandAll();

signals:
    void foldingChanged();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    struct FoldRegion
    {
        int first;
        int last;
        bool collapsed = false;
    };

    QWidget *foldArea;
    QVector<FoldRegion> regions;
    QMap<int, int> matchingBraces;
    QString foldingSource;
    bool updatingCode = false;

    int foldForLine(int line) const;
    int foldAt(const QPoint &point) const;
    void findFoldRegions(const QString &source);
    void applyFolding();
    void revealCursor();
    void updateFoldAreaGeometry();
    void paintFoldArea();
};

#endif // DECOMPILERTEXTEDIT_H
