#pragma once
#ifndef SESSION_H
#define SESSION_H

#include <WS2tcpip.h>
#include <unordered_set>
#include <mutex>
#include <array>
#include <atomic>
#include "protocol.h"
#include "include/lua.hpp"
#include "over_exp.h"

using namespace std;

// 세션 상태
enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };


// 세션 클래스 정의
class SESSION {
    OVER_EXP _recv_over;

public:
    std::mutex _s_lock;
    S_STATE _state;
    N_TYPE _npc_type;       // NPC 종류
    N_TYPE _npc_move_type;  // NPC 이동 방식
    bool _npc_attack;
    int _npc_target;
    std::atomic<bool> _is_active;
    int _id;
    SOCKET _socket;
    short x, y;
    char _name[NAME_SIZE];
    int _prev_remain;
    unordered_set<int> _view_list;
    mutex _vl;
    int last_move_time;
    lua_State* _L;
    mutex _ll;
    int _npc_move_time;
    bool _send_chat;
    int _player;
    int _damage;
    int _hp;
    int _max_hp;
    int _level;
    int _exp;
    POSITION _start_position;
    chrono::system_clock::time_point hp_time;
    std::unordered_set<int> cloud_view_list;
    chrono::system_clock::time_point m_npc_move_time;
    chrono::system_clock::time_point m_attack_time;

    SESSION();
    ~SESSION();

    void do_recv();
    void do_send(void* packet);
    void send_login_info_packet();
    void send_move_packet(int c_id);
    void send_add_player_packet(int c_id);
    void send_chat_packet(int c_id, const char* mess);
    void send_attack_packet(int a_id, int t_id);
    void send_ingameinfo_packet();
    void send_remove_player_packet(int c_id);
};

// 외부에서 참조할 전역 객체들
extern array<SESSION, MAX_USER + MAX_NPC> clients;

// 유틸리티 함수
bool is_pc(int object_id);
bool is_npc(int object_id);

#endif // SESSION_H
