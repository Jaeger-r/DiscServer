#include "CMySql.h"

namespace {
constexpr const char* kConnectionName = "disk_server_connection";
}

CMySql::CMySql(void)
{
    if (QSqlDatabase::contains(kConnectionName)) {
        m_pDataBase = QSqlDatabase::database(kConnectionName);
    } else {
        m_pDataBase = QSqlDatabase::addDatabase("QPSQL", kConnectionName);
    }
}

CMySql::~CMySql(void)
{
}

void CMySql::DisConnect()
{
    if (m_pDataBase.isOpen()) {
        m_pDataBase.close();
    }
}

bool CMySql::ConnectMySql(const char *host,const char *user,const char *pass,const char *db)
{
    return ConnectPostgreSql(host, 5432, user, pass, db);
}

bool CMySql::ConnectPostgreSql(const char* host, int port, const char* user, const char* pass, const char* db)
{
    m_pDataBase.setHostName(QString::fromLocal8Bit(host));
    m_pDataBase.setPort(port);
    m_pDataBase.setUserName(QString::fromLocal8Bit(user));
    m_pDataBase.setPassword(QString::fromLocal8Bit(pass));
    m_pDataBase.setDatabaseName(QString::fromLocal8Bit(db));
    return m_pDataBase.open();
}

bool CMySql::SelectMySql(const char* szSql,int nColumn,list<string>& lstStr)
{
    if (!szSql) {
        return false;
    }

    m_lastQuery = QSqlQuery(m_pDataBase);
    if (!m_lastQuery.exec(QString::fromLocal8Bit(szSql))) {
        return false;
    }

    while (m_lastQuery.next()) {
        for (int i = 0; i < nColumn; ++i) {
            QVariant value = m_lastQuery.value(i);
            if (value.isNull()) {
                lstStr.push_back("null");
            } else {
                lstStr.push_back(value.toString().toStdString());
            }
        }
    }

    return true;
}

bool CMySql::UpdateMySql(const char* szSql)
{
    if (!szSql) {
        return false;
    }

    m_lastQuery = QSqlQuery(m_pDataBase);
    if (!m_lastQuery.exec(QString::fromLocal8Bit(szSql))) {
        return false;
    }

    return true;
}

QString CMySql::escapeString(const QString& value) const
{
    QString escaped = value;
    escaped.replace('\'', QStringLiteral("''"));
    return escaped;
}

QString CMySql::lastErrorText() const
{
    if (m_lastQuery.lastError().isValid()) {
        return m_lastQuery.lastError().text();
    }
    return m_pDataBase.lastError().text();
}

qint64 CMySql::lastInsertId() const
{
    return m_lastQuery.lastInsertId().toLongLong();
}

bool CMySql::execStatements(const QStringList& statements)
{
    for (const QString& statement : statements) {
        const QString trimmed = statement.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        m_lastQuery = QSqlQuery(m_pDataBase);
        if (!m_lastQuery.exec(trimmed)) {
            return false;
        }
    }
    return true;
}
