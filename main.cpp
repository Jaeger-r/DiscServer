#include <QCoreApplication>
#include "./kernel/TCPKernel.h"
#include <iostream>
#include <QDir>
#include <QProcessEnvironment>
#include <QDebug>
#include <QFileInfo>
#include <QSettings>

using namespace std;
int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    TCPKernel *p = static_cast<TCPKernel*>(TCPKernel::getKernel());
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString configPath = QDir(appDir).filePath(QStringLiteral("diskserver.ini"));
    QSettings settings(configPath, QSettings::IniFormat);
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString defaultStorageRoot = QDir(appDir).filePath(QStringLiteral("storage"));
    const QString defaultLogPath = QDir(appDir).filePath(QStringLiteral("chat_history.txt"));
    const QString defaultListenHost = QStringLiteral("127.0.0.1");
    const quint16 defaultListenPort = 1234;
    const bool defaultTlsEnabled = true;
    const QString defaultTlsCertPath = QDir(appDir).filePath(QStringLiteral("tls/server.crt"));
    const QString defaultTlsKeyPath = QDir(appDir).filePath(QStringLiteral("tls/server.key"));
    const QString defaultDbHost = QStringLiteral("127.0.0.1");
    const int defaultDbPort = 5432;
    const QString defaultDbUser = QStringLiteral("jaeger_server");
    const QString defaultDbPassword = QStringLiteral("zhangwenjie172");
    const QString defaultDbName = QStringLiteral("netdisk");

    if (!QFileInfo::exists(configPath)) {
        settings.setValue(QStringLiteral("network/listenHost"), defaultListenHost);
        settings.setValue(QStringLiteral("network/listenPort"), defaultListenPort);
        settings.setValue(QStringLiteral("network/tlsEnabled"), defaultTlsEnabled);
        settings.setValue(QStringLiteral("network/tlsCertPath"), defaultTlsCertPath);
        settings.setValue(QStringLiteral("network/tlsKeyPath"), defaultTlsKeyPath);
        settings.setValue(QStringLiteral("paths/storageRoot"), defaultStorageRoot);
        settings.setValue(QStringLiteral("paths/chatLogPath"), defaultLogPath);
        settings.setValue(QStringLiteral("database/host"), defaultDbHost);
        settings.setValue(QStringLiteral("database/port"), defaultDbPort);
        settings.setValue(QStringLiteral("database/user"), defaultDbUser);
        settings.setValue(QStringLiteral("database/password"), defaultDbPassword);
        settings.setValue(QStringLiteral("database/name"), defaultDbName);
        settings.sync();
    }

    const QString storageRoot = env.value(QStringLiteral("DISKSERVER_STORAGE_ROOT"),
                                          settings.value(QStringLiteral("paths/storageRoot"), defaultStorageRoot).toString());
    const QString logPath = env.value(QStringLiteral("DISKSERVER_LOG_PATH"),
                                      settings.value(QStringLiteral("paths/chatLogPath"), defaultLogPath).toString());
    const QString listenHost = settings.value(QStringLiteral("network/listenHost"), defaultListenHost).toString().trimmed();
    const quint16 listenPort =
        static_cast<quint16>(settings.value(QStringLiteral("network/listenPort"), defaultListenPort).toUInt());
    const bool tlsEnabled =
        settings.value(QStringLiteral("network/tlsEnabled"), defaultTlsEnabled).toBool();
    const QString configuredTlsCertPath =
        settings.value(QStringLiteral("network/tlsCertPath"), defaultTlsCertPath).toString().trimmed();
    const QString configuredTlsKeyPath =
        settings.value(QStringLiteral("network/tlsKeyPath"), defaultTlsKeyPath).toString().trimmed();
    const QString tlsCertPath = QDir::isAbsolutePath(configuredTlsCertPath)
        ? configuredTlsCertPath
        : QDir(appDir).filePath(configuredTlsCertPath);
    const QString tlsKeyPath = QDir::isAbsolutePath(configuredTlsKeyPath)
        ? configuredTlsKeyPath
        : QDir(appDir).filePath(configuredTlsKeyPath);
    const QString dbHost = env.value(QStringLiteral("DISKSERVER_DB_HOST"),
                                     settings.value(QStringLiteral("database/host"), defaultDbHost).toString());
    const int dbPort = env.value(QStringLiteral("DISKSERVER_DB_PORT"),
                                 settings.value(QStringLiteral("database/port"), defaultDbPort).toString()).toInt();
    const QString dbUser = env.value(QStringLiteral("DISKSERVER_DB_USER"),
                                     settings.value(QStringLiteral("database/user"), defaultDbUser).toString());
    const QString dbPassword = env.value(QStringLiteral("DISKSERVER_DB_PASSWORD"),
                                         settings.value(QStringLiteral("database/password"), defaultDbPassword).toString());
    const QString dbName = env.value(QStringLiteral("DISKSERVER_DB_NAME"),
                                     settings.value(QStringLiteral("database/name"), defaultDbName).toString());
    qInfo() << "DiskServer config"
            << "configPath=" << configPath
            << "listenHost=" << listenHost
            << "listenPort=" << listenPort
            << "tlsEnabled=" << tlsEnabled
            << "tlsCertPath=" << tlsCertPath
            << "storageRoot=" << storageRoot
            << "dbHost=" << dbHost
            << "dbUser=" << dbUser
            << "dbName=" << dbName;
    p->applyRuntimePaths(storageRoot,
                         logPath,
                         listenHost,
                         listenPort,
                         tlsEnabled,
                         tlsCertPath,
                         tlsKeyPath,
                         dbHost,
                         dbUser,
                         dbPassword,
                         dbName,
                         dbPort);
    if(p->open()){
        cout<<"Server is running"<<endl;
    }else{
        cout<<"Server failed"<<endl;
        return 1;
    }

    //STRU_GETFILELIST_RQ sgr;
    //sgr.m_userId = 1;
    //p->dealData(0,(char*)&sgr);
    return a.exec();
}
