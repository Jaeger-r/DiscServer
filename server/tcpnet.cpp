#include "tcpnet.h"

#include <QFile>
#include <QHostAddress>
#include <QDebug>

#include "../kernel/TCPKernel.h"

TCPNet::TCPNet(QObject* parent)
    : QObject(parent)
{
    m_server.setConnectionHandler([this](qintptr socketDescriptor) {
        handleIncomingDescriptor(socketDescriptor);
    });
}

TCPNet::~TCPNet()
{
    unInitNetWork("server destroyed");
}

bool TCPNet::initNetWork(const char* szip, quint16 sport)
{
    if (m_tlsEnabled && !loadTlsMaterial()) {
        qWarning() << "DiskServer TLS material load failed.";
        return false;
    }

    QHostAddress hostAddress(QString::fromLocal8Bit(szip));
    if (hostAddress.isNull()) {
        hostAddress = QHostAddress::Any;
    }
    return m_server.listen(hostAddress, sport);
}

void TCPNet::configureTls(bool enabled, const QString& certificatePath, const QString& privateKeyPath)
{
    m_tlsEnabled = enabled;
    m_certificatePath = certificatePath;
    m_privateKeyPath = privateKeyPath;
}

void TCPNet::unInitNetWork(const char* szerr)
{
    Q_UNUSED(szerr);

    const auto sockets = m_clients.values();
    for (QSslSocket* socket : sockets) {
        if (!socket) {
            continue;
        }
        socket->disconnect(this);
        socket->disconnectFromHost();
        if (socket->state() != QAbstractSocket::UnconnectedState) {
            socket->waitForDisconnected(1000);
        }
        socket->deleteLater();
    }

    m_clients.clear();
    m_socketIds.clear();
    m_buffers.clear();

    if (m_server.isListening()) {
        m_server.close();
    }
}

bool TCPNet::sendData(ConnectionId sock, const char* szbuf, int nlen)
{
    if (!m_clients.contains(sock) || !szbuf || nlen <= 0) {
        return false;
    }

    QSslSocket* socket = m_clients.value(sock, nullptr);
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return false;
    }

    qint32 packetSize = nlen;
    ConnectionBuffer& state = m_buffers[sock];
    state.writeBuffer.append(reinterpret_cast<const char*>(&packetSize), sizeof(packetSize));
    state.writeBuffer.append(szbuf, nlen);

    const qint64 written = socket->write(state.writeBuffer);
    if (written < 0) {
        return false;
    }
    if (written > 0) {
        state.writeBuffer.remove(0, static_cast<int>(written));
    }
    return true;
}

QList<ConnectionId> TCPNet::connectionIds() const
{
    return m_clients.keys();
}

void TCPNet::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        auto* socket = m_server.nextPendingConnection();
        if (socket) {
            socket->deleteLater();
        }
    }
}

void TCPNet::handleIncomingDescriptor(qintptr socketDescriptor)
{
    auto* socket = new QSslSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        qWarning() << "DiskServer failed to adopt socket descriptor" << socketDescriptor << socket->errorString();
        socket->deleteLater();
        return;
    }

    const ConnectionId id = m_nextConnectionId++;
    m_clients.insert(id, socket);
    m_socketIds.insert(socket, id);
    m_buffers.insert(id, {});

    const QString peer = QStringLiteral("%1:%2")
        .arg(socket->peerAddress().toString())
        .arg(socket->peerPort());
    qInfo() << "DiskServer client accepted" << id << peer;
    TCPKernel::getKernel()->logRuntimeEvent(
        QStringLiteral("client accepted: sock=%1 peer=%2 tls=%3")
            .arg(id)
            .arg(peer)
            .arg(m_tlsEnabled ? QStringLiteral("enabled") : QStringLiteral("disabled")));

    connect(socket, &QSslSocket::readyRead, this, &TCPNet::onReadyRead);
    connect(socket, &QSslSocket::disconnected, this, &TCPNet::onSocketDisconnected);
    connect(socket, &QSslSocket::bytesWritten, this, &TCPNet::onBytesWritten);
    connect(socket, &QSslSocket::encrypted, this, &TCPNet::onEncrypted);
    connect(socket, &QSslSocket::sslErrors, this, &TCPNet::onSslErrors);

    if (m_tlsEnabled) {
        QSslConfiguration configuration = socket->sslConfiguration();
        configuration.setProtocol(QSsl::TlsV1_2OrLater);
        configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
        configuration.setLocalCertificate(m_serverCertificate);
        configuration.setPrivateKey(m_serverPrivateKey);
        socket->setSslConfiguration(configuration);
        socket->startServerEncryption();
    } else {
        handleEncryptedSocket(socket);
    }
}

void TCPNet::onReadyRead()
{
    auto* socket = qobject_cast<QSslSocket*>(sender());
    processSocketData(socket);
}

void TCPNet::processSocketData(QSslSocket* socket)
{
    if (!socket) {
        return;
    }

    if (m_tlsEnabled && !socket->isEncrypted()) {
        return;
    }

    const ConnectionId id = m_socketIds.value(socket, 0);
    if (id == 0) {
        return;
    }
    ConnectionBuffer& state = m_buffers[id];
    state.buffer.append(socket->readAll());

    while (true) {
        if (state.pendingPacketSize < 0) {
            if (state.buffer.size() < static_cast<int>(sizeof(qint32))) {
                return;
            }

            qint32 packetSize = 0;
            memcpy(&packetSize, state.buffer.constData(), sizeof(qint32));
            state.buffer.remove(0, sizeof(qint32));

            if (packetSize <= 0) {
                state.pendingPacketSize = -1;
                continue;
            }

            state.pendingPacketSize = packetSize;
        }

        if (state.buffer.size() < state.pendingPacketSize) {
            return;
        }

        QByteArray payload = state.buffer.left(state.pendingPacketSize);
        state.buffer.remove(0, state.pendingPacketSize);
        state.pendingPacketSize = -1;

        TCPKernel::getKernel()->dealData(id, payload.data());
    }
}

void TCPNet::onSocketDisconnected()
{
    auto* socket = qobject_cast<QSslSocket*>(sender());
    if (!socket) {
        return;
    }
    const ConnectionId id = m_socketIds.value(socket, 0);
    emit socketDisconnected(id);
    removeSocket(socket, id);
}

void TCPNet::onBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);
    auto* socket = qobject_cast<QSslSocket*>(sender());
    if (!socket) {
        return;
    }

    const ConnectionId id = m_socketIds.value(socket, 0);
    if (!m_buffers.contains(id)) {
        return;
    }

    ConnectionBuffer& state = m_buffers[id];
    if (state.writeBuffer.isEmpty()) {
        return;
    }

    const qint64 written = socket->write(state.writeBuffer);
    if (written > 0) {
        state.writeBuffer.remove(0, static_cast<int>(written));
    }
}

void TCPNet::onEncrypted()
{
    auto* socket = qobject_cast<QSslSocket*>(sender());
    handleEncryptedSocket(socket);
}

void TCPNet::handleEncryptedSocket(QSslSocket* socket)
{
    if (!socket) {
        return;
    }
    const ConnectionId id = m_socketIds.value(socket, 0);
    qInfo() << "DiskServer TLS encrypted connection established for socket" << id;
    TCPKernel::getKernel()->logRuntimeEvent(
        QStringLiteral("client tls encrypted: sock=%1 peer=%2:%3")
            .arg(id)
            .arg(socket->peerAddress().toString())
            .arg(socket->peerPort()));
    if (socket->bytesAvailable() > 0) {
        processSocketData(socket);
    }
}

void TCPNet::onSslErrors(const QList<QSslError>& errors)
{
    auto* socket = qobject_cast<QSslSocket*>(sender());
    if (!socket) {
        return;
    }

    QStringList messages;
    for (const QSslError& error : errors) {
        messages.push_back(error.errorString());
    }
    qWarning() << "DiskServer TLS errors for socket"
               << m_socketIds.value(socket, 0)
               << messages.join(QStringLiteral("; "));
    socket->disconnectFromHost();
}

void TCPNet::removeSocket(QSslSocket* socket, ConnectionId idHint)
{
    if (!socket) {
        return;
    }

    const ConnectionId id = idHint != 0 ? idHint : m_socketIds.value(socket, 0);
    m_clients.remove(id);
    m_socketIds.remove(socket);
    m_buffers.remove(id);
    socket->deleteLater();
}

bool TCPNet::loadTlsMaterial()
{
    QFile certFile(m_certificatePath);
    if (!certFile.open(QIODevice::ReadOnly)) {
        qWarning() << "DiskServer TLS certificate open failed:" << m_certificatePath;
        return false;
    }
    const QByteArray certBytes = certFile.readAll();
    certFile.close();

    QFile keyFile(m_privateKeyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        qWarning() << "DiskServer TLS private key open failed:" << m_privateKeyPath;
        return false;
    }
    const QByteArray keyBytes = keyFile.readAll();
    keyFile.close();

    m_serverCertificate = QSslCertificate(certBytes, QSsl::Pem);
    m_serverPrivateKey = QSslKey(keyBytes, QSsl::Rsa, QSsl::Pem);
    if (m_serverCertificate.isNull() || m_serverPrivateKey.isNull()) {
        qWarning() << "DiskServer TLS certificate or private key parse failed.";
        return false;
    }
    return true;
}
