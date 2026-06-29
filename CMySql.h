#ifndef CMYSQL_H
#define CMYSQL_H
#include <QtSql>
#include <QSqlQuery>
#include <list>
#include <string>
#include <QVariant>
#include <QSqlError>
#include <QStringList>
using namespace std;

class CMySql
{
public:
    CMySql(void);
    ~CMySql(void);
public:               //ip,                   用户名，    密码，            数据库 ，3306
    bool  ConnectMySql(const char *host,const char *user,const char *pass,const char *db);
    bool  ConnectPostgreSql(const char *host, int port, const char *user, const char *pass, const char *db);
    void  DisConnect();
    bool  SelectMySql(const char* szSql,int nColumn,list<string>& lstStr);

    //更新：删除、插入、修改
    bool  UpdateMySql(const char* szSql);
    QString escapeString(const QString& value) const;
    QString lastErrorText() const;
    qint64 lastInsertId() const;
    bool execStatements(const QStringList& statements);

private:
   QSqlDatabase m_pDataBase;
   QSqlQuery m_lastQuery;

};
#endif // CMYSQL_H
