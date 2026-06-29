#ifndef TCPKERNEL_H
#define TCPKERNEL_H

#include "IKernel.h"
#include"../server/tcpnet.h"
#include "CMySql.h"
#include "Packdef.h"
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QSettings>
#include <QTextStream>
#include <QString>
#include <fstream>
#include <map>
#include <string>

struct uploadFileInfo{
    QFile* m_file = nullptr;
    long long m_fileSize = 0;
    long long m_pos = 0;
    long long m_userId = 0;
    ConnectionId m_sock = 0;
    QString m_filePath;
    QString m_fileName;
    QString m_fileMd5;
};

struct downloadFileInfo {
    QFile* m_file = nullptr;
    ConnectionId m_sock = 0;
    long long m_fileId = 0;
    long long m_fileSize = 0;
    long long m_pos = 0;
    long long m_userId = 0;
    QString m_fileName;
    QString m_fileMd5;
};

using namespace std;

constexpr int DISKSERVER_PATH_BUFFER = 1024;

class TCPKernel : public IKernel
{
private:
    TCPKernel() ;
    ~TCPKernel();
public:
    virtual bool open();
    virtual void close();
    virtual void dealData(ConnectionId sock,char*szbuf);
    void logRuntimeEvent(const QString& message);
    void applyRuntimePaths(const QString& storageRoot,
                           const QString& logPath,
                           const QString& listenHost,
                           quint16 listenPort,
                           bool tlsEnabled,
                           const QString& tlsCertPath,
                           const QString& tlsKeyPath,
                           const QString& dbHost,
                           const QString& dbUser,
                           const QString& dbPassword,
                           const QString& dbName,
                           int dbPort = 5432);
public:
    void Register_Request(ConnectionId sock,char* szbuf);
    void Login_Request(ConnectionId sock,char* szbuf);
    void GetFileList_Request(ConnectionId sock,char* szbuf);
    void UploadFileInfo_Request(ConnectionId sock,char* szbuf);
    void UploadFileBlock_Request(ConnectionId sock,char* szbuf);
    void DownloadFileInfo_Request(ConnectionId sock,char* szbuf);
    void DownFileBlock_Request(ConnectionId sock,char* szbuf);
    void DeleteFile_Request(ConnectionId sock,char* szbuf);
    void RenameFile_Request(ConnectionId sock,char* szbuf);
    void SendMessage_Request(ConnectionId sock,char* szbuf);
    void OnlineUsers_Request(ConnectionId sock, char* szbuf);
    void PrivateChat_Request(ConnectionId sock, char* szbuf);
    void PrivateHistory_Request(ConnectionId sock, char* szbuf);
    void TransferControl_Request(ConnectionId sock, char* szbuf);
public:
    //单例模式--不支持线程安全
    //饿汉模式，支持线程安全 高效
    static TCPKernel * getKernel(){
        if (!m_pKernel) {
            m_pKernel = new TCPKernel;
        }
        return m_pKernel;
    }
private:
    bool ensureSchema();
    QString userStoragePath(long long userId) const;
    QString filePathForUser(long long userId, const QString& fileName) const;
    QString runtimeLogPath() const;
    void writeRuntimeLog(const QString& message);
    void ensureUserFileMapping(long long userId, long long fileId, const QString& displayName = QString());
    void registerOnlineSession(long long userId, ConnectionId sock);
    void unregisterOnlineSession(ConnectionId sock);
    QString userNameById(long long userId);
    void fillOnlineUsersResponse(STRU_ONLINE_USERS_RS& response, long long requesterId);
    void sendOnlineUsersToAll();
    void notifyUserFileSync(long long userId, char action, const QString& fileMd5, const QString& fileName, ConnectionId excludeSock = 0);
    long long syncFileReferenceCount(long long fileId);
    void cleanupUpload(long long fileId);
    void cleanupDownload(ConnectionId sock, long long fileId = 0);
    void handleDisconnected(ConnectionId sock);

    INet *m_pTCPNet;
    CMySql *m_pSQL;
    static TCPKernel *m_pKernel;
    char m_szSystemPath[DISKSERVER_PATH_BUFFER];
    QString m_storageRootPath;
    QString m_logFilePath;
    QString m_runtimeLogFilePath;
    QString m_listenHost = QStringLiteral("127.0.0.1");
    quint16 m_listenPort = 1234;
    bool m_tlsEnabled = true;
    QString m_tlsCertPath;
    QString m_tlsKeyPath;
    QString m_dbHost = QStringLiteral("127.0.0.1");
    QString m_dbUser = QStringLiteral("jaeger_server");
    QString m_dbPassword = QStringLiteral("zhangwenjie172");
    QString m_dbName = QStringLiteral("netdisk");
    int m_dbPort = 5432;
    std::map<long long,uploadFileInfo *> m_mapFileToFileInfo;
    QHash<long long, QHash<ConnectionId, downloadFileInfo*>> m_downloadByFile;
    QHash<long long, QSet<ConnectionId>> m_userConnections;
    QHash<ConnectionId, long long> m_connectionUsers;
    ofstream outFile;
};

#endif // TCPKERNEL_H
