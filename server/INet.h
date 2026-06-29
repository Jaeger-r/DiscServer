#ifndef INET_H
#define INET_H

#include <QList>
#include <QtGlobal>

using ConnectionId = qintptr;

class INet
{
public:
    virtual ~INet() = default;

    virtual bool initNetWork(const char* szip = "127.0.0.1", quint16 sport = 1234) = 0;
    virtual void unInitNetWork(const char* szerr) = 0;
    virtual bool sendData(ConnectionId sock, const char* szbuf, int nlen) = 0;
    virtual QList<ConnectionId> connectionIds() const = 0;
};

#endif // INET_H
