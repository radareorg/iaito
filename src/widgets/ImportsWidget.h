#ifndef IMPORTSWIDGET_H
#define IMPORTSWIDGET_H

#include <memory>

#include <QAbstractListModel>
#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QTreeWidgetItem>

#include "IaitoDockWidget.h"
#include "core/Iaito.h"
#include "widgets/ListDockWidget.h"
#include "common/AddressableItemModel.h"

class MainWindow;
//class QTreeWidget;
class QTreeWidgetItem;
class ImportsWidget;

class ImportsModel : public AddressableItemModel<QAbstractListModel>
{
    Q_OBJECT

    friend ImportsWidget;


private:
    QList<ImportDescription> *imports;
public:
    const QRegularExpression unsafe_banned = QRegularExpression(QStringLiteral(
                                                             "\\A(\\w\\.)*(system|strcpy|strcpyA|strcpyW|wcscpy|_tcscpy|_mbscpy|StrCpy|StrCpyA|StrCpyW|lstrcpy|lstrcpyA|lstrcpyW"
                                                             "|_tccpy|_mbccpy|_ftcscpy|strcat|strcatA|strcatW|wcscat|_tcscat|_mbscat|StrCat|StrCatA|StrCatW|lstrcat|lstrcatA|"
                                                             "lstrcatW|StrCatBuff|StrCatBuffA|StrCatBuffW|StrCatChainW|_tccat|_mbccat|_ftcscat|sprintfW|sprintfA|wsprintf|wsprintfW|"
                                                             "wsprintfA|sprintf|swprintf|_stprintf|wvsprintf|wvsprintfA|wvsprintfW|vsprintf|_vstprintf|vswprintf|strncpy|wcsncpy|"
                                                             "_tcsncpy|_mbsncpy|_mbsnbcpy|StrCpyN|StrCpyNA|StrCpyNW|StrNCpy|strcpynA|StrNCpyA|StrNCpyW|lstrcpyn|lstrcpynA|lstrcpynW|"
                                                             "strncat|wcsncat|_tcsncat|_mbsncat|_mbsnbcat|StrCatN|StrCatNA|StrCatNW|StrNCat|StrNCatA|StrNCatW|lstrncat|lstrcatnA|"
                                                             "lstrcatnW|lstrcatn|gets|_getts|_gettws|IsBadWritePtr|IsBadHugeWritePtr|IsBadReadPtr|IsBadHugeReadPtr|IsBadCodePtr|"
                                                             "IsBadStringPtr|memcpy|RtlCopyMemory|CopyMemory|wmemcpy|wnsprintf|wnsprintfA|wnsprintfW|_snwprintf|_snprintf|_sntprintf|"
                                                             "_vsnprintf|vsnprintf|_vsnwprintf|_vsntprintf|wvnsprintf|wvnsprintfA|wvnsprintfW|strtok|_tcstok|wcstok|_mbstok|makepath|"
                                                             "_tmakepath| _makepath|_wmakepath|_splitpath|_tsplitpath|_wsplitpath|scanf|wscanf|_tscanf|sscanf|swscanf|_stscanf|snscanf|"
                                                             "snwscanf|_sntscanf|_itoa|_itow|_i64toa|_i64tow|_ui64toa|_ui64tot|_ui64tow|_ultoa|_ultot|_ultow|CharToOem|CharToOemA|CharToOemW|"
                                                             "OemToChar|OemToCharA|OemToCharW|CharToOemBuffA|CharToOemBuffW|alloca|_alloca|strlen|wcslen|_mbslen|_mbstrlen|StrLen|lstrlen|"
                                                             "ChangeWindowMessageFilter)\\z"
                                                         ));
    const QRegularExpression thread_banned = QRegularExpression(QStringLiteral(
							"asctime$|crypt$|ctime$|drand48$|ecvt$|encrypt$|erand48$|ether_aton$|ether_ntoa$|fcvt$|fgetgrent$|fgetpwent$|fgetspent$|getaliasbyname$"
							"|getaliasent$|getdate$|getgrent$|getgrgid$|getgrnam$|gethostbyaddr$|gethostbyname2$|gethostbyname$|gethostent$|getlogin$|getmntent$"
							"|getnetbyaddr$|getnetbyname$|getnetent$|getnetgrent$|getprotobyname$|getprotobynumber$|getprotoent$|getpwent$|getpwnam$|getpwuid$"
							"|getrpcbyname$|getrpcbynumber$|getrpcent$|getservbyname$|getservbyport$|getservent$|getspent$|getspnam$|getutent$|getutid$|getutline$"
							"|gmtime$|hcreate$$|hdestroy$|hsearch$|initstate$|jrand48$|lcong48$|lgammaf$|lgammal$|lgamma$|localtime$|lrand48$|mrand48$|nrand48$|ptsname$"
							"|qecvt$|qfcvt$|qsort$|random$|rand$|readdir$|seed48$|setkey$|setstate$|sgetspent$|srand48$|srandom$|strerror$|strtok$|tmpnam$|ttyname$|twalk$\\z"
                                                         ));

public:
    enum Column { AddressColumn = 0, TypeColumn, LibraryColumn, NameColumn, SafetyColumn, CommentColumn, ColumnCount };
    enum Role { ImportDescriptionRole = Qt::UserRole, AddressRole };

    ImportsModel(QList<ImportDescription> *imports, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override;

    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    RVA address(const QModelIndex &index) const override;
    QString name(const QModelIndex &index) const override;
    QString libname(const QModelIndex &index) const;

};

class ImportsProxyModel : public AddressableFilterProxyModel
{
    Q_OBJECT

public:
    ImportsProxyModel(ImportsModel *sourceModel, QObject *parent = nullptr);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};

class ImportsWidget : public ListDockWidget
{
    Q_OBJECT

public:
    explicit ImportsWidget(MainWindow *main);
    ~ImportsWidget();

private slots:
    void refreshImports();
private:
    QList<ImportDescription> imports;
    ImportsModel *importsModel;
    ImportsProxyModel *importsProxyModel;

    void highlightUnsafe();
};

#endif // IMPORTSWIDGET_H
