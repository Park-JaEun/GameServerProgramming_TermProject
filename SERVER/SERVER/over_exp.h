#pragma once
#ifndef OVER_EXP_H
#define OVER_EXP_H

#include <WS2tcpip.h>
#include <MSWSock.h>
#include <cstring>
#include "protocol.h"

enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_NPC_MOVE, OP_PLAYER_MOVE };

class OVER_EXP {
public:
    WSAOVERLAPPED _over;
    WSABUF _wsabuf;
    char _send_buf[BUF_SIZE];
    COMP_TYPE _comp_type;
    int _ai_target_obj;

    OVER_EXP();
    OVER_EXP(char* packet);
};

#endif // OVER_EXP_H
