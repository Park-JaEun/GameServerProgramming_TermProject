#include "session.h"
#include <iostream>
#include <cstring>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <chrono>

using namespace std;

extern array<SESSION, MAX_USER + MAX_NPC> clients;

// 세션 클래스 구현
SESSION::SESSION()
{
    _id = -1;
    _socket = 0;
    x = y = 0;
    _name[0] = 0;
    _state = ST_FREE;
    _prev_remain = 0;
    _npc_move_time = 0;
    _send_chat = false;
    _player = -1;
    _npc_attack = false;
    _npc_target = -1;
    m_npc_move_time = chrono::system_clock::now();
    m_attack_time = chrono::system_clock::now();
}

SESSION::~SESSION() {}

void SESSION::do_recv()
{
    DWORD recv_flag = 0;
    memset(&_recv_over._over, 0, sizeof(_recv_over._over));
    _recv_over._wsabuf.len = BUF_SIZE - _prev_remain;
    _recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain;
    WSARecv(_socket, &_recv_over._wsabuf, 1, 0, &recv_flag, &_recv_over._over, 0);
}

void SESSION::do_send(void* packet)
{
    OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
    WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, &sdata->_over, 0);
}

void SESSION::send_login_info_packet()
{
    SC_LOGIN_INFO_PACKET p;
    p.id = _id;
    p.size = sizeof(SC_LOGIN_INFO_PACKET);
    p.type = SC_LOGIN_INFO;
    p.x = x;
    p.y = y;
    p.hp = _hp;
    p.max_hp = _max_hp;
    p.level = _level;
    p.exp = _exp;
    p.damage = _damage;

    strncpy_s(p.name, _name, NAME_SIZE);
    do_send(&p);
}

void SESSION::send_move_packet(int c_id)
{
    SC_MOVE_OBJECT_PACKET p;
    p.id = c_id;
    p.size = sizeof(SC_MOVE_OBJECT_PACKET);
    p.type = SC_MOVE_OBJECT;
    p.x = clients[c_id].x;
    p.y = clients[c_id].y;
    p.move_time = clients[c_id].last_move_time;
    do_send(&p);
}

void SESSION::send_add_player_packet(int c_id)
{
    SC_ADD_OBJECT_PACKET add_packet;
    add_packet.id = c_id;
    strcpy_s(add_packet.name, clients[c_id]._name);
    add_packet.size = sizeof(add_packet);
    add_packet.type = SC_ADD_OBJECT;
    add_packet.x = clients[c_id].x;
    add_packet.y = clients[c_id].y;
    add_packet.level = clients[c_id]._level;
    add_packet.damage = clients[c_id]._damage;
    add_packet.npc_type = clients[c_id]._npc_type;
    add_packet.npc_move_type = clients[c_id]._npc_move_type;

    _vl.lock();
    _view_list.insert(c_id);
    _vl.unlock();

    do_send(&add_packet);
}

void SESSION::send_chat_packet(int p_id, const char* mess)
{
    SC_CHAT_PACKET packet;
    packet.id = p_id;
    packet.size = sizeof(packet);
    packet.type = SC_CHAT;
    strcpy_s(packet.mess, mess);
    do_send(&packet);
}

void SESSION::send_attack_packet(int a_id, int t_id)
{
    SC_ATTACK_PACKET packet;
    packet.attack_id = t_id;
    packet.size = sizeof(SC_ATTACK_PACKET);
    packet.type = SC_ATTACK;
    packet.attack_id = a_id;
    packet.target_id = t_id;

    do_send(&packet);
}

void SESSION::send_ingameinfo_packet()
{
    SC_USER_INGAMEINFO_PACKET packet;
    packet.size = sizeof(SC_USER_INGAMEINFO_PACKET);
    packet.type = SC_USER_INGAMEINFO;
    packet.level = _level;
    packet.hp = _hp;
    packet.exp = _exp;

    do_send(&packet);
}

void SESSION::send_remove_player_packet(int c_id)
{
    _vl.lock();
    if (_view_list.count(c_id))
        _view_list.erase(c_id);
    else {
        _vl.unlock();
        return;
    }
    _vl.unlock();

    SC_REMOVE_OBJECT_PACKET p;
    p.id = c_id;
    p.size = sizeof(p);
    p.type = SC_REMOVE_OBJECT;
    do_send(&p);
}

// 전역 유틸리티 함수
bool is_pc(int object_id)
{
    return object_id < MAX_USER;
}

bool is_npc(int object_id)
{
    return !is_pc(object_id);
}
