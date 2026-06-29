#include "TCPKernel.h"

#include <QFileInfo>
#include <QSqlQuery>
#include <QTextStream>

TCPKernel* TCPKernel::m_pKernel = nullptr;

namespace {
QString safeText(const char* value)
{
    return QString::fromLocal8Bit(value).trimmed();
}

bool userOwnsFile(CMySql* sql, long long userId, long long fileId)
{
    if (!sql || userId <= 0 || fileId <= 0) {
        return false;
    }

    char ownerSql[512] = {0};
    snprintf(ownerSql, sizeof(ownerSql),
             "select 1 from user_file where u_id = %lld and f_id = %lld",
             userId, fileId);
    list<string> rows;
    return sql->SelectMySql(ownerSql, 1, rows) && !rows.empty();
}

QString userDisplayNameForFile(CMySql* sql, long long userId, long long fileId)
{
    if (!sql || userId <= 0 || fileId <= 0) {
        return {};
    }

    char displaySql[512] = {0};
    snprintf(displaySql, sizeof(displaySql),
             "select display_name from user_file where u_id = %lld and f_id = %lld",
             userId, fileId);
    list<string> rows;
    if (!sql->SelectMySql(displaySql, 1, rows) || rows.empty()) {
        return {};
    }
    const QString value = QString::fromStdString(rows.front()).trimmed();
    if (value.compare(QStringLiteral("null"), Qt::CaseInsensitive) == 0) {
        return {};
    }
    return value;
}
}

TCPKernel::TCPKernel()
{
    m_pTCPNet = new TCPNet;
    m_pSQL = new CMySql;
    QObject::connect(dynamic_cast<TCPNet*>(m_pTCPNet), &TCPNet::socketDisconnected, [this](ConnectionId id) {
        handleDisconnected(id);
    });
    m_storageRootPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("storage"));
    m_logFilePath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("chat_history.txt"));
    m_runtimeLogFilePath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("server-runtime.log"));
    QDir().mkpath(m_storageRootPath);
    const QByteArray rootBytes = QDir::toNativeSeparators(m_storageRootPath).toLocal8Bit() + QByteArray(1, '/');
    qstrncpy(m_szSystemPath, rootBytes.constData(), DISKSERVER_PATH_BUFFER);
}

TCPKernel::~TCPKernel()
{
    close();
    delete m_pTCPNet;
    delete m_pSQL;
}

void TCPKernel::applyRuntimePaths(const QString& storageRoot,
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
                                  int dbPort)
{
    if (!storageRoot.trimmed().isEmpty()) {
        m_storageRootPath = storageRoot.trimmed();
    }
    if (!logPath.trimmed().isEmpty()) {
        m_logFilePath = logPath.trimmed();
    }
    m_runtimeLogFilePath = runtimeLogPath();
    if (!listenHost.trimmed().isEmpty()) {
        m_listenHost = listenHost.trimmed();
    }
    if (listenPort > 0) {
        m_listenPort = listenPort;
    }
    m_tlsEnabled = tlsEnabled;
    if (!tlsCertPath.trimmed().isEmpty()) {
        m_tlsCertPath = tlsCertPath.trimmed();
    }
    if (!tlsKeyPath.trimmed().isEmpty()) {
        m_tlsKeyPath = tlsKeyPath.trimmed();
    }
    if (!dbHost.trimmed().isEmpty()) {
        m_dbHost = dbHost.trimmed();
    }
    if (!dbUser.trimmed().isEmpty()) {
        m_dbUser = dbUser.trimmed();
    }
    m_dbPassword = dbPassword;
    if (!dbName.trimmed().isEmpty()) {
        m_dbName = dbName.trimmed();
    }
    if (dbPort > 0) {
        m_dbPort = dbPort;
    }

    QDir().mkpath(m_storageRootPath);
    const QByteArray rootBytes = QDir::toNativeSeparators(m_storageRootPath).toLocal8Bit() + QByteArray(1, '/');
    qstrncpy(m_szSystemPath, rootBytes.constData(), DISKSERVER_PATH_BUFFER);
}

bool TCPKernel::open()
{
    if (auto* tcpNet = dynamic_cast<TCPNet*>(m_pTCPNet)) {
        tcpNet->configureTls(m_tlsEnabled, m_tlsCertPath, m_tlsKeyPath);
    }
    const QByteArray listenHostBytes = m_listenHost.toLocal8Bit();
    if (!m_pTCPNet->initNetWork(listenHostBytes.constData(), m_listenPort)) {
        qWarning() << "DiskServer listen failed on" << m_listenHost << ":" << m_listenPort;
        writeRuntimeLog(QStringLiteral("listen failed on %1:%2").arg(m_listenHost).arg(m_listenPort));
        return false;
    }
    writeRuntimeLog(QStringLiteral("listen success on %1:%2").arg(m_listenHost).arg(m_listenPort));

    if (!m_pSQL->ConnectPostgreSql(m_dbHost.toLocal8Bit().constData(),
                                   m_dbPort,
                                   m_dbUser.toLocal8Bit().constData(),
                                   m_dbPassword.toLocal8Bit().constData(),
                                   m_dbName.toLocal8Bit().constData())) {
        qWarning() << "DiskServer database connect failed:" << m_pSQL->lastErrorText();
        writeRuntimeLog(QStringLiteral("database connect failed: %1").arg(m_pSQL->lastErrorText()));
        return false;
    }

    if (!ensureSchema()) {
        qWarning() << "DiskServer schema initialization failed:" << m_pSQL->lastErrorText();
        writeRuntimeLog(QStringLiteral("schema initialization failed: %1").arg(m_pSQL->lastErrorText()));
        return false;
    }

    outFile.open(QDir::toNativeSeparators(m_logFilePath).toStdString(), ios::app);
    if (!outFile.is_open()) {
        qWarning() << "DiskServer chat log open failed:" << m_logFilePath;
        writeRuntimeLog(QStringLiteral("chat log open failed: %1").arg(m_logFilePath));
    }
    writeRuntimeLog(QStringLiteral("server started, listen=%1:%2, tls=%3, storageRoot=%4, dbHost=%5, dbUser=%6, dbName=%7, chatLog=%8")
                        .arg(m_listenHost)
                        .arg(m_listenPort)
                        .arg(m_tlsEnabled ? QStringLiteral("enabled") : QStringLiteral("disabled"))
                        .arg(m_storageRootPath, m_dbHost, m_dbUser, m_dbName, m_logFilePath));
    return true;
}

void TCPKernel::close()
{
    for (auto& pair : m_mapFileToFileInfo) {
        if (pair.second && pair.second->m_file) {
            pair.second->m_file->close();
            delete pair.second->m_file;
        }
        delete pair.second;
    }
    m_mapFileToFileInfo.clear();

    for (auto fileIt = m_downloadByFile.begin(); fileIt != m_downloadByFile.end(); ++fileIt) {
        for (auto* info : fileIt.value()) {
            if (info && info->m_file) {
                info->m_file->close();
                delete info->m_file;
            }
            delete info;
        }
    }
    m_downloadByFile.clear();

    if (m_pTCPNet) {
        m_pTCPNet->unInitNetWork("over");
    }
    if (m_pSQL) {
        m_pSQL->DisConnect();
    }
    if (outFile.is_open()) {
        outFile.close();
    }
    writeRuntimeLog(QStringLiteral("server stopped"));
}

void TCPKernel::dealData(ConnectionId sock, char* szbuf)
{
    if (!szbuf) {
        return;
    }

    writeRuntimeLog(QStringLiteral("packet received: sock=%1 type=%2")
                        .arg(sock)
                        .arg(static_cast<int>(*szbuf)));

    switch (*szbuf) {
    case _default_protocol_register_request:
        Register_Request(sock, szbuf);
        break;
    case _default_protocol_login_request:
        Login_Request(sock, szbuf);
        break;
    case _default_protocol_getfilelist_request:
        GetFileList_Request(sock, szbuf);
        break;
    case _default_protocol_uploadfileinfo_request:
        UploadFileInfo_Request(sock, szbuf);
        break;
    case _default_protocol_uoloadfileblock_request:
        UploadFileBlock_Request(sock, szbuf);
        break;
    case _default_protocol_downloadfileinfo_request:
        DownloadFileInfo_Request(sock, szbuf);
        break;
    case _default_protocol_downloadfileblock_request:
        DownFileBlock_Request(sock, szbuf);
        break;
    case _default_protocol_deletefile_request:
        DeleteFile_Request(sock, szbuf);
        break;
    case _default_protocol_renamefile_request:
        RenameFile_Request(sock, szbuf);
        break;
    case _default_protocol_chat_request:
        SendMessage_Request(sock, szbuf);
        break;
    case _default_protocol_online_users_request:
        OnlineUsers_Request(sock, szbuf);
        break;
    case _default_protocol_private_chat_request:
        PrivateChat_Request(sock, szbuf);
        break;
    case _default_protocol_private_history_request:
        PrivateHistory_Request(sock, szbuf);
        break;
    case _default_protocol_transfercontrol_request:
        TransferControl_Request(sock, szbuf);
        break;
    default:
        break;
    }
}

void TCPKernel::Register_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_REGISTER_RQ*>(szbuf);
    STRU_REGISTER_RS response;
    response.m_szResult = _register_err;

    const QString userName = m_pSQL->escapeString(safeText(request->m_szName));
    const QString password = m_pSQL->escapeString(safeText(request->m_szPassWord));

    char sql[SQLLEN * 2] = {0};
    snprintf(sql, sizeof(sql),
             "insert into user_account(u_name,u_password,u_tel) values ('%s','%s',%lld) on conflict (u_tel) do nothing",
             userName.toLocal8Bit().constData(),
             password.toLocal8Bit().constData(),
             request->m_tel);

    if (m_pSQL->UpdateMySql(sql)) {
        list<string> rows;
        char selectSql[SQLLEN] = {0};
        snprintf(selectSql, sizeof(selectSql),
                 "select u_id from user_account where u_tel = %lld",
                 request->m_tel);
        if (m_pSQL->SelectMySql(selectSql, 1, rows) && !rows.empty()) {
            response.m_szResult = _register_success;
            const long long userId = atoll(rows.front().c_str());
            QDir().mkpath(userStoragePath(userId));
            writeRuntimeLog(QStringLiteral("register success: sock=%1 userId=%2 userName=%3")
                                .arg(sock).arg(userId).arg(safeText(request->m_szName)));
        }
    }
    if (response.m_szResult != _register_success) {
        writeRuntimeLog(QStringLiteral("register failed: sock=%1 userName=%2 tel=%3")
                            .arg(sock).arg(safeText(request->m_szName)).arg(request->m_tel));
    }

    m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
}

void TCPKernel::Login_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_LOGIN_RQ*>(szbuf);
    STRU_LOGIN_RS response;
    response.m_szResult = _login_usernoexist;
    response.m_userId = 0;

    const QString userName = m_pSQL->escapeString(safeText(request->m_szName));
    char sql[SQLLEN] = {0};
    snprintf(sql, sizeof(sql),
             "select u_id,u_password from user_account where u_name = '%s'",
             userName.toLocal8Bit().constData());

    list<string> rows;
    if (m_pSQL->SelectMySql(sql, 2, rows) && rows.size() >= 2) {
        response.m_szResult = _login_passworderr;
        const string userId = rows.front();
        rows.pop_front();
        const string password = rows.front();
        if (strcmp(request->m_szPassWord, password.c_str()) == 0) {
            response.m_szResult = _login_success;
            response.m_userId = atoll(userId.c_str());
            QDir().mkpath(userStoragePath(response.m_userId));
            registerOnlineSession(response.m_userId, sock);
        }
    }

    writeRuntimeLog(QStringLiteral("login %1: sock=%2 userName=%3 userId=%4")
                        .arg(response.m_szResult == _login_success ? QStringLiteral("success") : QStringLiteral("failed"))
                        .arg(sock)
                        .arg(safeText(request->m_szName))
                        .arg(response.m_userId));

    m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
    if (response.m_szResult == _login_success) {
        sendOnlineUsersToAll();
    }
}

void TCPKernel::GetFileList_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_GETFILELIST_RQ*>(szbuf);
    writeRuntimeLog(QStringLiteral("file list request: sock=%1 userId=%2")
                        .arg(sock).arg(request->m_userId));
    char sql[SQLLEN] = {0};
    snprintf(sql, sizeof(sql),
             "select fi.f_id,coalesce(uf.display_name, fi.f_name),fi.f_size,to_char(fi.f_uploadtime,'YYYY-MM-DD HH24:MI:SS'),fi.f_md5,fi.f_path "
             "from user_file uf join file_info fi on uf.f_id = fi.f_id "
             "where uf.u_id = %lld order by fi.f_uploadtime desc",
             request->m_userId);

    list<string> rows;
    if (!m_pSQL->SelectMySql(sql, 6, rows)) {
        return;
    }

    STRU_GETFILELIST_RS response;
    int index = 0;
    while (rows.size() >= 6) {
        const long long fileId = atoll(rows.front().c_str());
        rows.pop_front();
        const string fileName = rows.front();
        rows.pop_front();
        const string fileSize = rows.front();
        rows.pop_front();
        const string uploadTime = rows.front();
        rows.pop_front();
        const string fileMd5 = rows.front();
        rows.pop_front();
        const QString filePath = QString::fromStdString(rows.front());
        rows.pop_front();

        char fileState = _filestate_ready;
        const qint64 expectedSize = atoll(fileSize.c_str());
        const QFileInfo fileInfo(filePath);
        const bool exists = fileInfo.exists();
        const qint64 actualSize = exists ? fileInfo.size() : 0;

        if (m_mapFileToFileInfo.find(fileId) != m_mapFileToFileInfo.end()) {
            fileState = _filestate_uploading;
        } else if (!exists) {
            fileState = _filestate_abnormal;
        } else if (actualSize < expectedSize) {
            fileState = _filestate_incomplete;
        } else if (actualSize > expectedSize) {
            fileState = _filestate_abnormal;
        }

        qstrncpy(response.m_aryInfo[index].m_szFileName, fileName.c_str(), MAXSIZE);
        qstrncpy(response.m_aryInfo[index].m_szFileDateTime, uploadTime.c_str(), MAXSIZE);
        response.m_aryInfo[index].m_fileSize = atoll(fileSize.c_str());
        qstrncpy(response.m_aryInfo[index].m_szFileMD5, fileMd5.c_str(), MAXSIZE);
        response.m_aryInfo[index].m_fileState = fileState;
        ++index;

        if (index == FILENUM || rows.empty()) {
            response.m_FileNum = index;
            m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
            memset(response.m_aryInfo, 0, sizeof(response.m_aryInfo));
            index = 0;
        }
    }
}

void TCPKernel::UploadFileInfo_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_UPLOADFILEINFO_RQ*>(szbuf);
    writeRuntimeLog(QStringLiteral("upload info request: sock=%1 userId=%2 fileName=%3 fileSize=%4 md5=%5")
                        .arg(sock)
                        .arg(request->m_userid)
                        .arg(safeText(request->m_szFileName))
                        .arg(request->m_filesize)
                        .arg(safeText(request->m_szFileMD5)));
    STRU_UPLOADFILEINFO_RS response;
    qstrncpy(response.m_szFileName, request->m_szFileName, MAXSIZE);
    qstrncpy(response.m_szFileMD5, request->m_szFileMD5, MAXSIZE);
    response.m_fileId = 0;
    response.m_pos = 0;
    response.m_szResult = _fileinfo_normal;

    const QString fileMd5 = m_pSQL->escapeString(safeText(request->m_szFileMD5));
    const QString fileName = m_pSQL->escapeString(safeText(request->m_szFileName));

    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "select f_id,f_path,f_size,coalesce(f_count,1) from file_info where f_md5 = '%s'",
             fileMd5.toLocal8Bit().constData());

    list<string> rows;
    if (m_pSQL->SelectMySql(sql, 4, rows) && rows.size() >= 4) {
        const long long fileId = atoll(rows.front().c_str());
        rows.pop_front();
        const QString filePath = QString::fromStdString(rows.front());
        rows.pop_front();
        const long long storedFileSize = atoll(rows.front().c_str());
        rows.pop_front();
        rows.pop_front();

        response.m_fileId = fileId;

        const QFileInfo existingFile(filePath);
        const bool mappingExists = userOwnsFile(m_pSQL, request->m_userid, fileId);
        const bool physicalExists = existingFile.exists();
        const qint64 diskSize = physicalExists ? existingFile.size() : 0;
        const qint64 expectedSize = request->m_filesize > 0 ? request->m_filesize : storedFileSize;

        if (m_mapFileToFileInfo.find(fileId) != m_mapFileToFileInfo.end()) {
            response.m_szResult = _fileinfo_busy;
            response.m_fileId = fileId;
            response.m_pos = diskSize;
            writeRuntimeLog(QStringLiteral("upload handshake busy: sock=%1 userId=%2 fileId=%3 md5=%4")
                                .arg(sock).arg(request->m_userid).arg(fileId).arg(safeText(request->m_szFileMD5)));
            m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
            return;
        }

        if (physicalExists && expectedSize > 0 && diskSize >= expectedSize) {
            if (storedFileSize != expectedSize) {
                char fixSizeSql[512] = {0};
                snprintf(fixSizeSql, sizeof(fixSizeSql),
                         "update file_info set f_size = %lld where f_id = %lld",
                         expectedSize,
                         fileId);
                m_pSQL->UpdateMySql(fixSizeSql);
            }
            if (!mappingExists) {
                ensureUserFileMapping(request->m_userid, fileId, safeText(request->m_szFileName));
                response.m_szResult = _fileinfo_speedtransfer;
            } else {
                response.m_szResult = _fileinfo_isuploaded;
            }
            writeRuntimeLog(QStringLiteral("upload handshake reused existing file: sock=%1 userId=%2 fileId=%3 result=%4")
                                .arg(sock).arg(request->m_userid).arg(fileId).arg(response.m_szResult));
            m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
            return;
        }

        auto* info = new uploadFileInfo;
        info->m_file = new QFile(filePath);
        info->m_fileSize = expectedSize;
        info->m_pos = qMax<qint64>(0, diskSize);
        info->m_userId = request->m_userid;
        info->m_sock = sock;
        info->m_filePath = filePath;
        info->m_fileName = safeText(request->m_szFileName);
        info->m_fileMd5 = safeText(request->m_szFileMD5);

        QDir().mkpath(QFileInfo(filePath).absolutePath());
        if (!info->m_file->open(QIODevice::ReadWrite)) {
            delete info->m_file;
            delete info;
            m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
            return;
        }

        if (info->m_pos > 0) {
            info->m_file->seek(info->m_pos);
        } else {
            info->m_file->resize(0);
            info->m_pos = 0;
        }

        m_mapFileToFileInfo[fileId] = info;
        response.m_pos = info->m_pos;
        response.m_szResult = info->m_pos > 0 ? _fileinfo_continue : _fileinfo_normal;
        notifyUserFileSync(request->m_userid, _filesync_action_upload_started, safeText(request->m_szFileMD5), safeText(request->m_szFileName), sock);
        writeRuntimeLog(QStringLiteral("upload handshake opened session: sock=%1 userId=%2 fileId=%3 pos=%4 result=%5")
                            .arg(sock).arg(request->m_userid).arg(fileId).arg(response.m_pos).arg(response.m_szResult));
        m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
        return;
    }

    const QString userDir = userStoragePath(request->m_userid);
    QDir().mkpath(userDir);
    const QString filePath = filePathForUser(request->m_userid, safeText(request->m_szFileName));

    snprintf(sql, sizeof(sql),
             "insert into file_info(f_name,f_size,f_path,f_md5,f_count) values('%s',%lld,'%s','%s',1)",
             fileName.toLocal8Bit().constData(),
             request->m_filesize,
             m_pSQL->escapeString(QDir::toNativeSeparators(filePath)).toLocal8Bit().constData(),
             fileMd5.toLocal8Bit().constData());

    if (!m_pSQL->UpdateMySql(sql)) {
        writeRuntimeLog(QStringLiteral("upload info insert failed: sock=%1 userId=%2 fileName=%3")
                            .arg(sock).arg(request->m_userid).arg(safeText(request->m_szFileName)));
        m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
        return;
    }

    response.m_fileId = m_pSQL->lastInsertId();

    char mappingSql[512] = {0};
    ensureUserFileMapping(request->m_userid, response.m_fileId, safeText(request->m_szFileName));

    auto* info = new uploadFileInfo;
    info->m_file = new QFile(filePath);
    info->m_fileSize = request->m_filesize;
    info->m_pos = 0;
    info->m_userId = request->m_userid;
    info->m_sock = sock;
    info->m_filePath = filePath;
    info->m_fileName = safeText(request->m_szFileName);
    info->m_fileMd5 = safeText(request->m_szFileMD5);
    const bool resumeExistingFile = info->m_file->exists();
    if (!info->m_file->open(QIODevice::ReadWrite)) {
        delete info->m_file;
        delete info;
    } else {
        if (resumeExistingFile) {
            info->m_pos = info->m_file->size();
            response.m_pos = info->m_pos;
            info->m_file->seek(info->m_pos);
        } else {
            info->m_file->resize(0);
        }
        m_mapFileToFileInfo[response.m_fileId] = info;
        notifyUserFileSync(request->m_userid, _filesync_action_upload_started, safeText(request->m_szFileMD5), safeText(request->m_szFileName), sock);
    }

    writeRuntimeLog(QStringLiteral("upload handshake created new session: sock=%1 userId=%2 fileId=%3 pos=%4")
                        .arg(sock).arg(request->m_userid).arg(response.m_fileId).arg(response.m_pos));

    m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
}

void TCPKernel::UploadFileBlock_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_UPLOADFILEBLOCK_RQ*>(szbuf);
    auto it = m_mapFileToFileInfo.find(request->m_fileId);
    if (it == m_mapFileToFileInfo.end() || !it->second || !it->second->m_file) {
        return;
    }

    uploadFileInfo* info = it->second;
    if (info->m_sock != sock) {
        return;
    }

    if (!info->m_file->isOpen()) {
        if (!info->m_file->open(QIODevice::ReadWrite)) {
            return;
        }
        info->m_file->seek(info->m_pos);
    }

    const qint64 written = info->m_file->write(request->m_szFileContent, request->m_fileNum);
    if (written > 0) {
        info->m_pos += written;
    }

    STRU_UPLOADFILEBLOCK_RS response;
    response.m_fileId = request->m_fileId;
    response.m_pos = info->m_pos;
    response.m_fileSize = info->m_fileSize;
    response.m_szResult = info->m_pos >= info->m_fileSize ? _transfer_result_finished : _transfer_result_running;
    m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));

    if (info->m_pos >= info->m_fileSize) {
        info->m_file->flush();
        info->m_file->close();
        ensureUserFileMapping(info->m_userId, request->m_fileId, info->m_fileName);
        notifyUserFileSync(info->m_userId, _filesync_action_upload_completed, info->m_fileMd5, info->m_fileName, sock);
        writeRuntimeLog(QStringLiteral("upload completed: sock=%1 userId=%2 fileId=%3 fileName=%4 size=%5")
                            .arg(sock).arg(info->m_userId).arg(request->m_fileId).arg(info->m_fileName).arg(info->m_fileSize));
        cleanupUpload(request->m_fileId);
    }
}

void TCPKernel::DownloadFileInfo_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_DOWNLOADFILEINFO_RQ*>(szbuf);
    writeRuntimeLog(QStringLiteral("download info request: sock=%1 userId=%2 fileName=%3 md5=%4 pos=%5")
                        .arg(sock)
                        .arg(request->m_userid)
                        .arg(safeText(request->m_szFileName))
                        .arg(safeText(request->m_szFileMD5))
                        .arg(request->m_pos));
    STRU_DOWNLOADFILEINFO_RS response;
    response.m_fileId = 0;
    response.m_pos = 0;
    response.m_szResult = _delete_noexit;
    qstrncpy(response.m_szFileMD5, request->m_szFileMD5, MAXSIZE);

    const QString fileMd5 = m_pSQL->escapeString(safeText(request->m_szFileMD5));
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "select fi.f_id,coalesce(uf.display_name, fi.f_name),fi.f_size,fi.f_path "
             "from file_info fi join user_file uf on fi.f_id = uf.f_id "
             "where uf.u_id = %lld and fi.f_md5 = '%s'",
             request->m_userid,
             fileMd5.toLocal8Bit().constData());

    list<string> rows;
    if (!(m_pSQL->SelectMySql(sql, 4, rows) && rows.size() >= 4)) {
        m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
        return;
    }

    const long long fileId = atoll(rows.front().c_str());
    rows.pop_front();
    const QString fileName = QString::fromStdString(rows.front());
    rows.pop_front();
    const long long fileSize = atoll(rows.front().c_str());
    rows.pop_front();
    const QString filePath = QString::fromStdString(rows.front());

    if (m_mapFileToFileInfo.find(fileId) != m_mapFileToFileInfo.end()) {
        response.m_fileId = fileId;
        response.m_szResult = _fileinfo_busy;
        m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
        return;
    }

    cleanupDownload(sock, fileId);

    auto* info = new downloadFileInfo;
    info->m_file = new QFile(filePath);
    info->m_sock = sock;
    info->m_fileId = fileId;
    info->m_fileSize = fileSize;
    info->m_userId = request->m_userid;
    info->m_fileName = fileName;
    info->m_fileMd5 = safeText(request->m_szFileMD5);

    if (!info->m_file->open(QIODevice::ReadOnly)) {
        delete info->m_file;
        delete info;
        m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
        return;
    }

    m_downloadByFile[fileId].insert(sock, info);
    qstrncpy(response.m_szFileName, fileName.toLocal8Bit().constData(), MAXSIZE);
    qstrncpy(response.m_szFileMD5, request->m_szFileMD5, MAXSIZE);
    response.m_fileId = fileId;
    response.m_pos = qBound<qint64>(0, request->m_pos, fileSize);
    info->m_pos = response.m_pos;
    info->m_file->seek(info->m_pos);
    response.m_szResult = _fileinfo_normal;
    writeRuntimeLog(QStringLiteral("download session opened: sock=%1 userId=%2 fileId=%3 fileName=%4 startPos=%5 fileSize=%6")
                        .arg(sock).arg(request->m_userid).arg(fileId).arg(fileName).arg(response.m_pos).arg(fileSize));
    m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
}

void TCPKernel::DownFileBlock_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_DOWNLOADFILEBLOCK_RQ*>(szbuf);
    if (!request || request->m_fileId <= 0) {
        return;
    }

    auto fileIt = m_downloadByFile.find(request->m_fileId);
    if (fileIt == m_downloadByFile.end() || !fileIt.value().contains(sock)) {
        return;
    }

    downloadFileInfo* info = fileIt.value().value(sock, nullptr);
    if (!info || !info->m_file) {
        return;
    }

    STRU_DOWNLOADFILEBLOCK_RS response;
    response.m_fileId = info->m_fileId;
    const QByteArray block = info->m_file->read(ONE_PAGE);
    response.m_fileNum = block.size();
    if (response.m_fileNum > 0) {
        memcpy(response.m_szFileContent, block.constData(), response.m_fileNum);
        m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
        info->m_pos += response.m_fileNum;
    }

    if (block.isEmpty() || info->m_pos >= info->m_fileSize) {
        writeRuntimeLog(QStringLiteral("download completed: sock=%1 userId=%2 fileId=%3 fileName=%4 size=%5")
                            .arg(sock).arg(info->m_userId).arg(info->m_fileId).arg(info->m_fileName).arg(info->m_fileSize));
        cleanupDownload(sock, info->m_fileId);
    }
}

void TCPKernel::DeleteFile_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_DELETEFILE_RQ*>(szbuf);
    writeRuntimeLog(QStringLiteral("delete request: sock=%1 userId=%2 md5=%3")
                        .arg(sock).arg(request->m_userId).arg(safeText(request->m_szFileMD5)));
    STRU_DELETEFILE_RS response;
    response.m_userId = request->m_userId;
    response.m_szResult = _delete_noexit;
    qstrncpy(response.m_szFileMD5, request->m_szFileMD5, MAXSIZE);

    const QString fileMd5 = m_pSQL->escapeString(safeText(request->m_szFileMD5));
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "select fi.f_id,fi.f_path from file_info fi join user_file uf on fi.f_id = uf.f_id "
             "where uf.u_id = %lld and fi.f_md5 = '%s'",
             request->m_userId,
             fileMd5.toLocal8Bit().constData());

    list<string> rows;
    if (!(m_pSQL->SelectMySql(sql, 2, rows) && rows.size() >= 2)) {
        m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
        return;
    }

    const long long fileId = atoll(rows.front().c_str());
    rows.pop_front();
    const QString filePath = QString::fromStdString(rows.front());
    const QString displayName = userDisplayNameForFile(m_pSQL, request->m_userId, fileId);

    char mappingSql[512] = {0};
    snprintf(mappingSql, sizeof(mappingSql),
             "delete from user_file where u_id = %lld and f_id = %lld",
             request->m_userId, fileId);
    m_pSQL->UpdateMySql(mappingSql);

    char countSql[256] = {0};
    snprintf(countSql, sizeof(countSql),
             "update file_info set f_count = greatest(f_count - 1, 0) where f_id = %lld",
             fileId);
    m_pSQL->UpdateMySql(countSql);

    char remainSql[256] = {0};
    snprintf(remainSql, sizeof(remainSql),
             "select f_count from file_info where f_id = %lld",
             fileId);
    list<string> remainRows;
    long long remainCount = 0;
    if (m_pSQL->SelectMySql(remainSql, 1, remainRows) && !remainRows.empty()) {
        remainCount = atoll(remainRows.front().c_str());
    }

    if (remainCount <= 0) {
        char cleanupSql[256] = {0};
        snprintf(cleanupSql, sizeof(cleanupSql),
                 "delete from file_info where f_id = %lld and f_count <= 0",
                 fileId);
        m_pSQL->UpdateMySql(cleanupSql);
        if (QFileInfo::exists(filePath)) {
            QFile::remove(filePath);
        }
    }

    response.m_szResult = _delete_success;
    notifyUserFileSync(request->m_userId, _filesync_action_delete, safeText(request->m_szFileMD5), displayName, sock);
    writeRuntimeLog(QStringLiteral("delete success: sock=%1 userId=%2 fileId=%3 path=%4")
                        .arg(sock).arg(request->m_userId).arg(fileId).arg(filePath));

    m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
}

void TCPKernel::RenameFile_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_RENAMEFILE_RQ*>(szbuf);
    STRU_RENAMEFILE_RS response;
    response.m_userId = request->m_userId;
    response.m_szResult = _rename_noexit;
    qstrncpy(response.m_szFileMD5, request->m_szFileMD5, MAXSIZE);
    qstrncpy(response.m_szNewFileName, request->m_szNewFileName, NAMESIZE);

    const QString fileMd5 = m_pSQL->escapeString(safeText(request->m_szFileMD5));
    const QString newFileNameRaw = safeText(request->m_szNewFileName);
    const QString newFileName = m_pSQL->escapeString(newFileNameRaw);

    writeRuntimeLog(QStringLiteral("rename request: sock=%1 userId=%2 md5=%3 newName=%4")
                        .arg(sock)
                        .arg(request->m_userId)
                        .arg(safeText(request->m_szFileMD5))
                        .arg(newFileNameRaw));

    if (newFileNameRaw.isEmpty()) {
        response.m_szResult = _rename_fail;
        m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
        return;
    }

    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "select fi.f_id from file_info fi join user_file uf on fi.f_id = uf.f_id "
             "where uf.u_id = %lld and fi.f_md5 = '%s'",
             request->m_userId,
             fileMd5.toLocal8Bit().constData());

    list<string> rows;
    if (!(m_pSQL->SelectMySql(sql, 1, rows) && !rows.empty())) {
        m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
        return;
    }

    const long long fileId = atoll(rows.front().c_str());
    char updateSql[1024] = {0};
    snprintf(updateSql, sizeof(updateSql),
             "update user_file set display_name = '%s' where u_id = %lld and f_id = %lld",
             newFileName.toLocal8Bit().constData(),
             request->m_userId,
             fileId);

    if (!m_pSQL->UpdateMySql(updateSql)) {
        response.m_szResult = _rename_fail;
        m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
        return;
    }

    response.m_szResult = _rename_success;
    notifyUserFileSync(request->m_userId, _filesync_action_rename, safeText(request->m_szFileMD5), newFileNameRaw, sock);
    writeRuntimeLog(QStringLiteral("rename success: sock=%1 userId=%2 fileId=%3 newName=%4")
                        .arg(sock)
                        .arg(request->m_userId)
                        .arg(fileId)
                        .arg(newFileNameRaw));
    m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
}

void TCPKernel::SendMessage_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_CHAT_RQ*>(szbuf);
    writeRuntimeLog(QStringLiteral("chat message: sock=%1 userName=%2 length=%3")
                        .arg(sock).arg(safeText(request->m_userName)).arg(strlen(request->szbuf)));
    STRU_CHAT_RS response;
    qstrncpy(response.m_userName, request->m_userName, MAXSIZE);
    qstrncpy(response.szbuf, request->szbuf, MAXSENDMESSSAGE);

    if (outFile.is_open()) {
        outFile << "[" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString()
                << "] " << request->m_userName << ": " << request->szbuf << '\n';
        outFile.flush();
    }

    const auto ids = m_pTCPNet->connectionIds();
    for (ConnectionId id : ids) {
        if (id == sock) {
            continue;
        }
        m_pTCPNet->sendData(id, reinterpret_cast<char*>(&response), sizeof(response));
    }
}

void TCPKernel::OnlineUsers_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_ONLINE_USERS_RQ*>(szbuf);
    STRU_ONLINE_USERS_RS response;
    response.m_userId = request ? request->m_userId : 0;
    fillOnlineUsersResponse(response, response.m_userId);
    m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
}

void TCPKernel::PrivateChat_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_PRIVATE_CHAT_RQ*>(szbuf);
    if (!request || request->m_senderId <= 0 || request->m_receiverId <= 0) {
        return;
    }

    const QString senderName = m_pSQL->escapeString(safeText(request->m_senderName));
    const QString receiverName = m_pSQL->escapeString(safeText(request->m_receiverName));
    const QString messageText = m_pSQL->escapeString(safeText(request->szbuf));

    char insertSql[SQLLEN * 8] = {0};
    snprintf(insertSql, sizeof(insertSql),
             "insert into private_chat_message(sender_id, receiver_id, sender_name, receiver_name, message_text) "
             "values(%lld, %lld, '%s', '%s', '%s')",
             request->m_senderId,
             request->m_receiverId,
             senderName.toLocal8Bit().constData(),
             receiverName.toLocal8Bit().constData(),
             messageText.toLocal8Bit().constData());
    if (!m_pSQL->UpdateMySql(insertSql)) {
        writeRuntimeLog(QStringLiteral("private chat save failed: %1").arg(m_pSQL->lastErrorText()));
    }

    STRU_PRIVATE_CHAT_RS response;
    response.m_senderId = request->m_senderId;
    response.m_receiverId = request->m_receiverId;
    response.m_offline = m_userConnections.contains(request->m_receiverId) ? 0 : 1;
    qstrncpy(response.m_senderName, request->m_senderName, MAXSIZE);
    qstrncpy(response.m_receiverName, request->m_receiverName, MAXSIZE);
    qstrncpy(response.szbuf, request->szbuf, MAXSENDMESSSAGE);

    const QSet<ConnectionId> receiverSockets = m_userConnections.value(request->m_receiverId);
    for (ConnectionId receiverSock : receiverSockets) {
        m_pTCPNet->sendData(receiverSock, reinterpret_cast<char*>(&response), sizeof(response));
    }

    const QSet<ConnectionId> senderSockets = m_userConnections.value(request->m_senderId);
    for (ConnectionId senderSock : senderSockets) {
        if (senderSock == sock) {
            continue;
        }
        m_pTCPNet->sendData(senderSock, reinterpret_cast<char*>(&response), sizeof(response));
    }

    writeRuntimeLog(QStringLiteral("private chat: from=%1 to=%2 length=%3 offline=%4")
                        .arg(request->m_senderId)
                        .arg(request->m_receiverId)
                        .arg(strlen(request->szbuf))
                        .arg(response.m_offline));
}

void TCPKernel::PrivateHistory_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_PRIVATE_HISTORY_RQ*>(szbuf);
    if (!request || request->m_userId <= 0 || request->m_peerId <= 0) {
        return;
    }

    STRU_PRIVATE_HISTORY_RS response;
    response.m_userId = request->m_userId;
    response.m_peerId = request->m_peerId;
    response.m_messageCount = 0;

    char sql[SQLLEN * 8] = {0};
    snprintf(sql, sizeof(sql),
             "select sender_id, receiver_id, sender_name, receiver_name, "
             "to_char(created_at,'YYYY-MM-DD HH24:MI:SS'), message_text "
             "from ("
             "  select * from private_chat_message "
             "  where (sender_id = %lld and receiver_id = %lld) "
             "     or (sender_id = %lld and receiver_id = %lld) "
             "  order by created_at desc, id desc limit %d"
             ") recent order by created_at asc, id asc",
             request->m_userId, request->m_peerId,
             request->m_peerId, request->m_userId,
             CHATHISTORYNUM);

    list<string> rows;
    if (m_pSQL->SelectMySql(sql, 6, rows)) {
        while (rows.size() >= 6 && response.m_messageCount < CHATHISTORYNUM) {
            ChatHistoryInfo& item = response.m_messages[response.m_messageCount++];
            item.m_senderId = atoll(rows.front().c_str());
            rows.pop_front();
            item.m_receiverId = atoll(rows.front().c_str());
            rows.pop_front();
            qstrncpy(item.m_senderName, QString::fromStdString(rows.front()).toLocal8Bit().constData(), MAXSIZE);
            rows.pop_front();
            qstrncpy(item.m_receiverName, QString::fromStdString(rows.front()).toLocal8Bit().constData(), MAXSIZE);
            rows.pop_front();
            qstrncpy(item.m_createdAt, QString::fromStdString(rows.front()).toLocal8Bit().constData(), MAXSIZE);
            rows.pop_front();
            qstrncpy(item.szbuf, QString::fromStdString(rows.front()).toLocal8Bit().constData(), MAXSENDMESSSAGE);
            rows.pop_front();
        }
    } else {
        writeRuntimeLog(QStringLiteral("private history query failed: %1").arg(m_pSQL->lastErrorText()));
    }

    m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
}

void TCPKernel::TransferControl_Request(ConnectionId sock, char* szbuf)
{
    auto* request = reinterpret_cast<STRU_TRANSFERCONTROL_RQ*>(szbuf);
    writeRuntimeLog(QStringLiteral("transfer control request: sock=%1 target=%2 action=%3 fileId=%4 md5=%5")
                        .arg(sock)
                        .arg(request->m_target)
                        .arg(request->m_action)
                        .arg(request->m_fileId)
                        .arg(safeText(request->m_szFileMD5)));
    STRU_TRANSFERCONTROL_RS response;
    response.m_target = request->m_target;
    response.m_action = request->m_action;
    response.m_fileId = request->m_fileId;
    response.m_szResult = _transfer_result_failed;
    qstrncpy(response.m_szFileMD5, request->m_szFileMD5, MAXSIZE);

    if (request->m_target == _transfer_target_upload) {
        auto it = m_mapFileToFileInfo.find(request->m_fileId);
        if (it != m_mapFileToFileInfo.end()) {
            uploadFileInfo* info = it->second;
            if (!info || info->m_sock != sock) {
                m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
                return;
            }
            if (request->m_action == _transfer_action_pause) {
                if (info->m_file) {
                    info->m_file->flush();
                }
                notifyUserFileSync(info->m_userId, _filesync_action_upload_paused, info->m_fileMd5, info->m_fileName, sock);
                cleanupUpload(request->m_fileId);
                response.m_szResult = _transfer_result_running;
            } else if (request->m_action == _transfer_action_cancel) {
                const QString filePath = info->m_filePath;
                const long long userId = info->m_userId;
                const QString fileMd5 = info->m_fileMd5;
                const QString fileName = info->m_fileName;
                cleanupUpload(request->m_fileId);
                QFile::remove(filePath);

                char mappingSql[512] = {0};
                snprintf(mappingSql, sizeof(mappingSql),
                         "delete from user_file where u_id = %lld and f_id = %lld",
                         userId, request->m_fileId);
                m_pSQL->UpdateMySql(mappingSql);

                const long long remainCount = syncFileReferenceCount(request->m_fileId);
                if (remainCount <= 0) {
                    char deleteSql[256] = {0};
                    snprintf(deleteSql, sizeof(deleteSql),
                             "delete from file_info where f_id = %lld",
                             request->m_fileId);
                    m_pSQL->UpdateMySql(deleteSql);
                }
                notifyUserFileSync(userId, _filesync_action_upload_cancelled, fileMd5, fileName, sock);
                response.m_szResult = _transfer_result_running;
            }
        }
    } else if (request->m_target == _transfer_target_download) {
        auto fileIt = m_downloadByFile.find(request->m_fileId);
        if (fileIt != m_downloadByFile.end() && fileIt.value().contains(sock)) {
            cleanupDownload(sock, request->m_fileId);
            response.m_szResult = _transfer_result_running;
        }
    }

    m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
}

bool TCPKernel::ensureSchema()
{
    const QStringList statements = {
        QStringLiteral("create table if not exists user_account ("
                       "u_id bigserial primary key,"
                       "u_name varchar(45) not null unique,"
                       "u_password varchar(45) not null,"
                       "u_tel bigint not null unique,"
                       "created_at timestamp not null default current_timestamp)"),
        QStringLiteral("create table if not exists file_info ("
                       "f_id bigserial primary key,"
                       "f_name varchar(255) not null,"
                       "f_size bigint not null,"
                       "f_uploadtime timestamp not null default current_timestamp,"
                       "f_path text not null,"
                       "f_count integer not null default 1,"
                       "f_md5 varchar(45) not null unique)"),
        QStringLiteral("create table if not exists user_file ("
                       "u_id bigint not null references user_account(u_id) on delete cascade,"
                       "f_id bigint not null references file_info(f_id) on delete cascade,"
                       "display_name varchar(255),"
                       "linked_at timestamp not null default current_timestamp,"
                       "primary key (u_id, f_id))"),
        QStringLiteral("alter table user_file add column if not exists display_name varchar(255)"),
        QStringLiteral("create table if not exists chat_message_log ("
                       "id bigserial primary key,"
                       "sender_name varchar(45) not null,"
                       "message_text text not null,"
                       "created_at timestamp not null default current_timestamp)"),
        QStringLiteral("create table if not exists private_chat_message ("
                       "id bigserial primary key,"
                       "sender_id bigint not null references user_account(u_id) on delete cascade,"
                       "receiver_id bigint not null references user_account(u_id) on delete cascade,"
                       "sender_name varchar(45) not null,"
                       "receiver_name varchar(45) not null,"
                       "message_text text not null,"
                       "created_at timestamp not null default current_timestamp)"),
        QStringLiteral("create index if not exists idx_private_chat_pair_time on private_chat_message "
                       "(least(sender_id, receiver_id), greatest(sender_id, receiver_id), created_at)"),
        QStringLiteral("create table if not exists share_link ("
                       "id bigserial primary key,"
                       "owner_u_id bigint not null references user_account(u_id) on delete cascade,"
                       "f_id bigint not null references file_info(f_id) on delete cascade,"
                       "share_code varchar(45) not null unique,"
                       "status smallint not null default 1,"
                       "created_at timestamp not null default current_timestamp,"
                       "expire_at timestamp)"),
        QStringLiteral("create table if not exists friend_relation ("
                       "u_id bigint not null references user_account(u_id) on delete cascade,"
                       "friend_u_id bigint not null references user_account(u_id) on delete cascade,"
                       "status smallint not null default 1,"
                       "created_at timestamp not null default current_timestamp,"
                       "primary key (u_id, friend_u_id),"
                       "check (u_id <> friend_u_id))")
    };

    return m_pSQL->execStatements(statements);
}

QString TCPKernel::userStoragePath(long long userId) const
{
    return QDir(m_storageRootPath).filePath(QString::number(userId));
}

QString TCPKernel::filePathForUser(long long userId, const QString& fileName) const
{
    return QDir(userStoragePath(userId)).filePath(fileName);
}

QString TCPKernel::runtimeLogPath() const
{
    return QDir(QFileInfo(m_logFilePath).absolutePath()).filePath(QStringLiteral("server-runtime.log"));
}

void TCPKernel::writeRuntimeLog(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        return;
    }

    const QFileInfo logInfo(m_runtimeLogFilePath);
    QDir().mkpath(logInfo.absolutePath());

    QFile file(m_runtimeLogFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "[" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "] "
           << message << '\n';
}

void TCPKernel::logRuntimeEvent(const QString& message)
{
    writeRuntimeLog(message);
}

void TCPKernel::ensureUserFileMapping(long long userId, long long fileId, const QString& displayName)
{
    if (userId <= 0 || fileId <= 0) {
        return;
    }

    const QString safeDisplayName = m_pSQL->escapeString(displayName.trimmed());
    if (!userOwnsFile(m_pSQL, userId, fileId)) {
        char mappingSql[1024] = {0};
        const QByteArray displayNameSql = safeDisplayName.isEmpty()
            ? QByteArray("null")
            : QStringLiteral("'%1'").arg(safeDisplayName).toLocal8Bit();
        snprintf(mappingSql, sizeof(mappingSql),
                 "insert into user_file(u_id,f_id,display_name) values(%lld,%lld,%s) on conflict do nothing",
                 userId,
                 fileId,
                 displayNameSql.constData());
        m_pSQL->UpdateMySql(mappingSql);
    } else if (!safeDisplayName.isEmpty() && userDisplayNameForFile(m_pSQL, userId, fileId).isEmpty()) {
        char updateSql[1024] = {0};
        snprintf(updateSql, sizeof(updateSql),
                 "update user_file set display_name = '%s' where u_id = %lld and f_id = %lld and (display_name is null or display_name = '')",
                 safeDisplayName.toLocal8Bit().constData(),
                 userId,
                 fileId);
        m_pSQL->UpdateMySql(updateSql);
    }

    syncFileReferenceCount(fileId);
}

void TCPKernel::registerOnlineSession(long long userId, ConnectionId sock)
{
    if (userId <= 0 || sock == 0) {
        return;
    }

    if (m_connectionUsers.contains(sock)) {
        const long long oldUserId = m_connectionUsers.value(sock);
        if (oldUserId == userId) {
            return;
        }
        unregisterOnlineSession(sock);
    }

    m_connectionUsers.insert(sock, userId);
    m_userConnections[userId].insert(sock);
}

void TCPKernel::unregisterOnlineSession(ConnectionId sock)
{
    if (!m_connectionUsers.contains(sock)) {
        return;
    }

    const long long userId = m_connectionUsers.take(sock);
    auto it = m_userConnections.find(userId);
    if (it == m_userConnections.end()) {
        return;
    }

    it->remove(sock);
    if (it->isEmpty()) {
        m_userConnections.erase(it);
    }
}

QString TCPKernel::userNameById(long long userId)
{
    if (userId <= 0) {
        return QString();
    }

    char sql[SQLLEN] = {0};
    snprintf(sql, sizeof(sql),
             "select u_name from user_account where u_id = %lld",
             userId);
    list<string> rows;
    if (m_pSQL->SelectMySql(sql, 1, rows) && !rows.empty()) {
        return QString::fromStdString(rows.front());
    }
    return QString();
}

void TCPKernel::fillOnlineUsersResponse(STRU_ONLINE_USERS_RS& response, long long requesterId)
{
    response.m_userId = requesterId;
    response.m_userCount = 0;

    list<string> rows;
    if (!m_pSQL->SelectMySql("select u_id,u_name from user_account order by u_name", 2, rows)) {
        writeRuntimeLog(QStringLiteral("online users query failed: %1").arg(m_pSQL->lastErrorText()));
        return;
    }

    while (rows.size() >= 2 && response.m_userCount < ONLINEUSERNUM) {
        const long long userId = atoll(rows.front().c_str());
        rows.pop_front();
        const QString userName = QString::fromStdString(rows.front());
        rows.pop_front();
        if (userId <= 0 || userId == requesterId) {
            continue;
        }
        OnlineUserInfo& item = response.m_users[response.m_userCount++];
        item.m_userId = userId;
        item.m_online = m_userConnections.contains(userId) ? 1 : 0;
        qstrncpy(item.m_userName, userName.toLocal8Bit().constData(), MAXSIZE);
    }
}

void TCPKernel::sendOnlineUsersToAll()
{
    for (auto it = m_connectionUsers.constBegin(); it != m_connectionUsers.constEnd(); ++it) {
        const ConnectionId sock = it.key();
        const long long userId = it.value();
        STRU_ONLINE_USERS_RS response;
        fillOnlineUsersResponse(response, userId);
        m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
    }
}

void TCPKernel::notifyUserFileSync(long long userId, char action, const QString& fileMd5, const QString& fileName, ConnectionId excludeSock)
{
    if (userId <= 0 || !m_userConnections.contains(userId)) {
        return;
    }

    STRU_FILESYNC_RS response;
    response.m_userId = userId;
    response.m_action = action;
    qstrncpy(response.m_szFileMD5, fileMd5.toLocal8Bit().constData(), MAXSIZE);
    qstrncpy(response.m_szFileName, fileName.toLocal8Bit().constData(), NAMESIZE);

    const QSet<ConnectionId> sockets = m_userConnections.value(userId);
    for (ConnectionId sock : sockets) {
        if (sock == excludeSock) {
            continue;
        }
        m_pTCPNet->sendData(sock, reinterpret_cast<char*>(&response), sizeof(response));
    }
}

long long TCPKernel::syncFileReferenceCount(long long fileId)
{
    if (fileId <= 0) {
        return 0;
    }

    char countSql[256] = {0};
    snprintf(countSql, sizeof(countSql),
             "select count(*) from user_file where f_id = %lld",
             fileId);
    list<string> rows;
    long long count = 0;
    if (m_pSQL->SelectMySql(countSql, 1, rows) && !rows.empty()) {
        count = atoll(rows.front().c_str());
    }

    char updateSql[256] = {0};
    snprintf(updateSql, sizeof(updateSql),
             "update file_info set f_count = %lld where f_id = %lld",
             count, fileId);
    m_pSQL->UpdateMySql(updateSql);
    return count;
}

void TCPKernel::cleanupUpload(long long fileId)
{
    auto it = m_mapFileToFileInfo.find(fileId);
    if (it == m_mapFileToFileInfo.end()) {
        return;
    }

    uploadFileInfo* info = it->second;
    if (info && info->m_file) {
        info->m_file->close();
        delete info->m_file;
    }
    delete info;
    m_mapFileToFileInfo.erase(it);
}

void TCPKernel::cleanupDownload(ConnectionId sock, long long fileId)
{
    if (fileId > 0) {
        auto fileIt = m_downloadByFile.find(fileId);
        if (fileIt == m_downloadByFile.end()) {
            return;
        }

        downloadFileInfo* info = fileIt.value().take(sock);
        if (!info) {
            return;
        }

        if (info->m_file) {
            info->m_file->close();
            delete info->m_file;
        }
        delete info;

        if (fileIt.value().isEmpty()) {
            m_downloadByFile.erase(fileIt);
        }
        return;
    }

    QList<long long> emptyFileIds;
    for (auto fileIt = m_downloadByFile.begin(); fileIt != m_downloadByFile.end(); ++fileIt) {
        downloadFileInfo* info = fileIt.value().take(sock);
        if (info) {
            if (info->m_file) {
                info->m_file->close();
                delete info->m_file;
            }
            delete info;
        }
        if (fileIt.value().isEmpty()) {
            emptyFileIds.append(fileIt.key());
        }
    }

    for (long long emptyFileId : emptyFileIds) {
        m_downloadByFile.remove(emptyFileId);
    }
}

void TCPKernel::handleDisconnected(ConnectionId sock)
{
    writeRuntimeLog(QStringLiteral("client disconnected: sock=%1").arg(sock));
    unregisterOnlineSession(sock);
    sendOnlineUsersToAll();
    cleanupDownload(sock);

    QList<long long> uploadIds;
    for (const auto& pair : m_mapFileToFileInfo) {
        if (pair.second && pair.second->m_sock == sock) {
            uploadIds.append(pair.first);
        }
    }

    for (long long fileId : uploadIds) {
        auto it = m_mapFileToFileInfo.find(fileId);
        if (it == m_mapFileToFileInfo.end() || !it->second) {
            continue;
        }
        if (it->second->m_file) {
            it->second->m_file->flush();
        }
        cleanupUpload(fileId);
    }
}
