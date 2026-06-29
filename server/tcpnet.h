#ifndef TCPNET_H
#define TCPNET_H

#include <QHash>
#include <QList>
#include <QByteArray>
#include <QTcpServer>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>

#include <functional>

#include "INet.h"

class DescriptorServer : public QTcpServer
{
public:
    using Handler = std::function<void(qintptr)>;

    explicit DescriptorServer(QObject* parent = nullptr)
        : QTcpServer(parent)
    {
    }

    void setConnectionHandler(Handler handler)
    {
        m_handler = std::move(handler);
    }

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        if (m_handler) {
            m_handler(socketDescriptor);
            return;
        }
        QTcpServer::incomingConnection(socketDescriptor);
    }

private:
    Handler m_handler;
};

class TCPNet : public QObject, public INet
{
    Q_OBJECT

public:
    explicit TCPNet(QObject* parent = nullptr);
    ~TCPNet() override;

    bool initNetWork(const char* szip = "127.0.0.1", quint16 sport = 1234) override;
    void configureTls(bool enabled, const QString& certificatePath, const QString& privateKeyPath);
    void unInitNetWork(const char* szerr) override;
    bool sendData(ConnectionId sock, const char* szbuf, int nlen) override;
    QList<ConnectionId> connectionIds() const override;

signals:
    void socketDisconnected(ConnectionId id);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();
    void onBytesWritten(qint64 bytes);
    void onEncrypted();
    void onSslErrors(const QList<QSslError>& errors);

private:
    struct ConnectionBuffer {
        QByteArray buffer;
        QByteArray writeBuffer;
        qint32 pendingPacketSize = -1;
    };

    void removeSocket(QSslSocket* socket, ConnectionId idHint = 0);
    bool loadTlsMaterial();
    void handleIncomingDescriptor(qintptr socketDescriptor);
    void handleEncryptedSocket(QSslSocket* socket);
    void processSocketData(QSslSocket* socket);

    DescriptorServer m_server;
    QHash<ConnectionId, QSslSocket*> m_clients;
    QHash<QSslSocket*, ConnectionId> m_socketIds;
    QHash<ConnectionId, ConnectionBuffer> m_buffers;
    ConnectionId m_nextConnectionId = 1;
    bool m_tlsEnabled = false;
    QString m_certificatePath;
    QString m_privateKeyPath;
    QSslCertificate m_serverCertificate;
    QSslKey m_serverPrivateKey;
};

#endif // TCPNET_H
