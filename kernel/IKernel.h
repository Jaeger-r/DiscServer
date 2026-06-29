#ifndef IKERNEL_H
#define IKERNEL_H

#include <QtGlobal>

using ConnectionId = qintptr;

class IKernel
{
public:
    virtual ~IKernel() = default;

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual void dealData(ConnectionId sock, char* szbuf) = 0;
};

#endif // IKERNEL_H
