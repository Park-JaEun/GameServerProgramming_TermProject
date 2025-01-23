#include <winsock2.h>
#include "worker_thread.h"
#include <iostream>
#include "protocol.h" // 필요한 헤더 파일 포함
#include <thread>
#include <chrono>
#include <unordered_set>
#include <vector>
#include "session.h" // 세션 관련 정의 포함
#include "over_exp.h"
#include <concurrent_priority_queue.h>

using namespace std;

extern array<SESSION, MAX_USER + MAX_NPC> clients;
extern HANDLE h_iocp;

extern bool g_obstacle_map[W_HEIGHT][W_WIDTH]; // false면 이동 가능, true면 장애물
extern SOCKET g_s_socket, g_c_socket;
extern OVER_EXP g_a_over;

std::vector<POSITION> g_cloud_list;

vector<int>sector[SECTOR_COUNT_Y][SECTOR_COUNT_X];

enum EVENT_TYPE { EV_RANDOM_MOVE, EV_MOVE_TARGET };

struct TIMER_EVENT {
	int obj_id;
	chrono::system_clock::time_point wakeup_time;
	EVENT_TYPE event_id;
	int target_id;
	constexpr bool operator < (const TIMER_EVENT& L) const
	{
		return (wakeup_time > L.wakeup_time);
	}
};

concurrency::concurrent_priority_queue<TIMER_EVENT> timer_queue;

bool in_npc_see(int from, int to)
{
	if (abs(clients[from].x - clients[to].x) > NPC_VIEW_RANGE) return false;
	return abs(clients[from].y - clients[to].y) <= NPC_VIEW_RANGE;
}

void do_timer()
{
	while (true) {
		TIMER_EVENT ev;
		auto current_time = chrono::system_clock::now();
		if (true == timer_queue.try_pop(ev)) {
			if (ev.wakeup_time > current_time) {
				timer_queue.push(ev);		// 최적화 필요
				// timer_queue에 다시 넣지 않고 처리해야 한다.
				this_thread::sleep_for(1ms);  // 실행시간이 아직 안되었으므로 잠시 대기
				continue;
			}
			switch (ev.event_id) {
			case EV_RANDOM_MOVE:
			{
				OVER_EXP* ov = new OVER_EXP;
				ov->_comp_type = OP_NPC_MOVE;
				ov->_ai_target_obj = clients[ev.obj_id]._player;
				PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ov->_over);
				break;
			}
			}
			continue;		// 즉시 다음 작업 꺼내기
		}
		this_thread::sleep_for(1ms);   // timer_queue가 비어 있으니 잠시 기다렸다가 다시 시작
	}
}

void disconnect(int c_id)
{
	clients[c_id]._vl.lock();
	unordered_set <int> vl = clients[c_id]._view_list;
	clients[c_id]._vl.unlock();
	for (auto& p_id : vl) {
		if (is_npc(p_id)) continue;
		auto& pl = clients[p_id];
		{
			std::lock_guard<std::mutex> ll(pl._s_lock);
			if (ST_INGAME != pl._state) continue;
		}
		if (pl._id == c_id) continue;
		pl.send_remove_player_packet(c_id);
	}
	closesocket(clients[c_id]._socket);

	std::lock_guard<std::mutex> ll(clients[c_id]._s_lock);

	clients[c_id]._state = ST_FREE;
}

bool can_see(int from, int to)
{
	if (abs(clients[from].x - clients[to].x) > VIEW_RANGE) return false;
	return abs(clients[from].y - clients[to].y) <= VIEW_RANGE;
}

void WakeUpNPC(int npc_id, int waker)
{
	OVER_EXP* exover = new OVER_EXP;
	exover->_comp_type = OP_PLAYER_MOVE;  // 플레이어가 이동했으니까 NPC를 꺠워라
	exover->_ai_target_obj = waker;       // 깨운 플레이어 정보
	PostQueuedCompletionStatus(h_iocp, 1, npc_id, &exover->_over);
	clients[npc_id]._player = waker;

	// atomic_compare_exchange_strong를 사용하여 원자적으로 상태 변경
	if (clients[npc_id]._is_active.load()) return; // 이미 활성 상태인지 확인
	bool old_state = false;
	if (!clients[npc_id]._is_active.compare_exchange_strong(old_state, true))
		return;  // 상태 변경 실패 시 반환

	TIMER_EVENT ev{ npc_id, chrono::system_clock::now(), EV_RANDOM_MOVE, 0 };
	timer_queue.push(ev);
}

bool movePossible(POSITION& player) {
	if (g_obstacle_map[player.y][player.x])
		return false;

	return true;
}

bool can_see_cloud(int c_x, int c_y, int cloud_x, int cloud_y)
{
	if (abs(c_x - cloud_x) > VIEW_RANGE) return false;
	return abs(c_y - cloud_y) <= VIEW_RANGE;
}

bool hit_success(POSITION from, POSITION to)
{
	if (to.x == from.x - 1 && to.y == from.y)
	{
		return true;
	}
	if (to.x == from.x && to.y == from.y - 1)
	{
		return true;
	}
	if (to.x == from.x + 1 && to.y == from.y)
	{
		return true;
	}
	if (to.x == from.x && to.y == from.y + 1)
	{
		return true;
	}
	else return false;
}

bool hit_success_A(POSITION from, POSITION to)
{
	if (to.x == from.x - 1 && to.y == from.y)
	{
		return true;
	}
	if (to.x == from.x && to.y == from.y - 1)
	{
		return true;
	}
	if (to.x == from.x + 1 && to.y == from.y)
	{
		return true;
	}
	if (to.x == from.x && to.y == from.y + 1)
	{
		return true;
	}
	if (to.x == from.x - 2 && to.y == from.y)
	{
		return true;
	}
	if (to.x == from.x && to.y == from.y - 2)
	{
		return true;
	}
	if (to.x == from.x + 2 && to.y == from.y)
	{
		return true;
	}
	if (to.x == from.x && to.y == from.y + 2)
	{
		return true;
	}
	else return false;
}

vector<int> getSectorCandidates(int x, int y) {
	vector<int> result;
	int sx = x / SECTOR_SIZE;
	int sy = y / SECTOR_SIZE;
	for (int yy = max(0, sy - 1); yy <= min(SECTOR_COUNT_Y - 1, sy + 1); yy++) {
		for (int xx = max(0, sx - 1); xx <= min(SECTOR_COUNT_X - 1, sx + 1); xx++) {
			for (auto pid : sector[yy][xx]) {
				result.push_back(pid);
			}
		}
	}
	return result;
}

void process_packet(int c_id, char* packet) {
	switch (packet[2]) {
	case CS_LOGIN: {
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
		// 클라이언트 이름 복사
		if (strcpy_s(clients[c_id]._name, NAME_SIZE, p->name) != 0) {
			std::cerr << "Error copying name for client ID: " << c_id << std::endl;
			return;
		}

		{
			lock_guard<mutex>ll{ clients[c_id]._s_lock };
			clients[c_id]._state = ST_INGAME;
			clients[c_id]._npc_type = NT_PLAYER;
			clients[c_id].x = rand() % W_WIDTH;
			clients[c_id].y = rand() % W_HEIGHT;
			clients[c_id]._hp = 10;
			clients[c_id]._max_hp = 10;
			clients[c_id]._damage = 1;
			clients[c_id]._level = 1;
			clients[c_id]._exp = 100;
			clients[c_id]._start_position = { clients[c_id].x,clients[c_id].y };
			clients[c_id].send_ingameinfo_packet();
		}
		clients[c_id].send_login_info_packet();
		for (auto& pl : clients) {
			{
				lock_guard<mutex>ll(pl._s_lock);
				if (ST_INGAME != pl._state)continue;
			}
			if (pl._id == c_id)continue;
			if (false == can_see(c_id, pl._id))continue;
			if (is_pc(pl._id))pl.send_add_player_packet(c_id);
			else WakeUpNPC(pl._id, c_id);
			clients[c_id].send_add_player_packet(pl._id);
		}
		for (int id = 0; id < (int)g_cloud_list.size(); id++) {
			if (!can_see_cloud(clients[c_id].x, clients[c_id].y, g_cloud_list[id].x, g_cloud_list[id].y))continue;
			clients[c_id].cloud_view_list.insert(id);
			SC_CLOUD_PACKET p;
			p.size = sizeof(SC_CLOUD_PACKET);
			p.type = SC_CLOUD;
			p.id = id;
			p.x = g_cloud_list[id].x;
			p.y = g_cloud_list[id].y;

			p.in_see = true;
			clients[c_id].do_send(&p);
		}
		break;
	}
	case CS_MOVE: {
		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
		clients[c_id].last_move_time = p->move_time;
		if (clients[c_id].m_npc_move_time < chrono::system_clock::now()) {
			short x = clients[c_id].x;
			short y = clients[c_id].y;
			switch (p->direction) {
			case 0:if (y > 0)y--; break;
			case 1:if (y < W_HEIGHT - 1)y++; break;
			case 2:if (x > 0)x--; break;
			case 3:if (x < W_WIDTH - 1)x++; break;
			}
			POSITION nextPos = { x,y };
			if (!movePossible(nextPos))break;
			clients[c_id].x = x;
			clients[c_id].y = y;
			unordered_set<int>near_list;
			clients[c_id]._vl.lock();
			unordered_set<int>old_vlist = clients[c_id]._view_list;
			clients[c_id]._vl.unlock();
			auto candidateList = getSectorCandidates(clients[c_id].x, clients[c_id].y);
			for (int pid : candidateList) {
				if (clients[pid]._state != ST_INGAME)continue;
				if (pid == c_id)continue;
				if (can_see(c_id, pid))near_list.insert(pid);
				if (clients[pid]._npc_type == NT_AGRO && clients[pid]._npc_attack == false && in_npc_see(c_id, pid))
					if (pid > MAX_USER) {
						clients[pid]._npc_attack = true;
						clients[pid]._npc_target = c_id;
					}
			}
			clients[c_id].send_move_packet(c_id);
			clients[c_id].m_npc_move_time = chrono::system_clock::now() + chrono::seconds(1);
			for (auto& pl : near_list) {
				auto& cpl = clients[pl];
				if (is_pc(pl)) {
					cpl._vl.lock();
					if (clients[pl]._view_list.count(c_id)) {
						cpl._vl.unlock();
						clients[pl].send_move_packet(c_id);
					}
					else {
						cpl._vl.unlock();
						clients[pl].send_add_player_packet(c_id);
					}
				}
				else WakeUpNPC(pl, c_id);
				if (old_vlist.count(pl) == 0)clients[c_id].send_add_player_packet(pl);
			}
			for (auto& pl : old_vlist)
				if (0 == near_list.count(pl)) {
					clients[c_id].send_remove_player_packet(pl);
					if (is_pc(pl))clients[pl].send_remove_player_packet(c_id);
				}
			for (int id = 0; id < (int)g_cloud_list.size(); id++) {
				if (can_see_cloud(clients[c_id].x, clients[c_id].y, g_cloud_list[id].x, g_cloud_list[id].y)) {
					if (clients[c_id].cloud_view_list.find(id) == clients[c_id].cloud_view_list.end()) {
						clients[c_id].cloud_view_list.insert(id);
						SC_CLOUD_PACKET p;
						p.size = sizeof(SC_CLOUD_PACKET);
						p.type = SC_CLOUD;
						p.id = id;
						p.x = g_cloud_list[id].x;
						p.y = g_cloud_list[id].y;
						p.in_see = true;
						clients[c_id].do_send(&p);
					}
				}
				else {
					if (clients[c_id].cloud_view_list.find(id) != clients[c_id].cloud_view_list.end()) {
						clients[c_id].cloud_view_list.erase(id);
						SC_CLOUD_PACKET p;
						p.size = sizeof(SC_CLOUD_PACKET);
						p.type = SC_CLOUD;
						p.in_see = false;
						p.id = id;
						clients[c_id].do_send(&p);
					}
				}
			}
		}
		break;
	}
	case CS_CHAT: {
		CS_CHAT_PACKET* p = reinterpret_cast<CS_CHAT_PACKET*>(packet);
		for (auto& pl : clients) {
			if (pl._state != ST_INGAME)continue;
			if (is_pc(pl._id))
				pl.send_chat_packet(c_id, p->mess);
		}
		break;
	}
	case CS_ATTACK: {
		if (clients[c_id].m_attack_time < chrono::system_clock::now()) {
			int x = clients[c_id].x;
			int y = clients[c_id].y;
			for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i) {
				if (true == hit_success(POSITION{ x,y }, POSITION{ clients[i].x,clients[i].y })) {
					clients[c_id].m_attack_time = chrono::system_clock::now() + chrono::seconds(1);
					clients[i]._hp -= clients[c_id]._damage;
					clients[c_id].send_attack_packet(c_id, i);
					if (clients[i]._npc_type == NT_PEACE && clients[i]._npc_attack == false) {
						clients[i]._npc_attack = true;
						clients[i]._npc_target = c_id;
					}
					if (clients[i]._hp <= 0 && clients[i]._state == ST_INGAME) {
						SC_DIE_PACKET p;
						p.id = i;
						p.size = sizeof(SC_DIE_PACKET);
						p.type = SC_DIE;
						for (auto& pl : clients) {
							if (pl._state != ST_INGAME)continue;
							if (is_pc(pl._id))
								pl.do_send(&p);
						}
						if (clients[i]._npc_type == NT_AGRO)
							clients[c_id]._exp += clients[i]._level * clients[i]._level * 4;
						else if (clients[i]._npc_move_type == NT_ROAM)
							clients[c_id]._exp += clients[i]._level * clients[i]._level * 4;
						else
							clients[c_id]._exp += clients[i]._level * clients[i]._level * 2;
						for (int z = clients[c_id]._level; z < 10; z++) {
							if (clients[c_id]._exp >= 100 * pow(2, z - 1))clients[c_id]._level = z;
							else break;
						}
						if (clients[i]._npc_type == NT_PEACE) {
							SC_ITEM_PACKET pp;
							pp.id = i;
							pp.size = sizeof(SC_ITEM_PACKET);
							pp.type = SC_ITEM;
							clients[c_id].do_send(&pp);
							clients[c_id]._hp += 10;
						}
						clients[c_id].send_ingameinfo_packet();
						clients[i]._state = ST_FREE;
					}
				}
			}
		}
		break;
	}
	case CS_NPC_WAKED: {
		CS_NPC_WAKED_PACKET* p = reinterpret_cast<CS_NPC_WAKED_PACKET*>(packet);
		clients[p->id]._state = ST_INGAME;
		clients[p->id]._hp = clients[p->id]._max_hp;
		clients[p->id]._npc_attack = false;
		clients[p->id]._npc_target = -1;
		break;
	}
	case CS_RECOVER: {
		clients[c_id]._hp += int(clients[c_id]._hp * 0.1f);
		clients[c_id].send_ingameinfo_packet();
		break;
	}
	case CS_ATTACK_A: {
		int x = clients[c_id].x;
		int y = clients[c_id].y;
		for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i) {
			if (true == hit_success_A(POSITION{ x,y }, POSITION{ clients[i].x,clients[i].y })) {
				clients[i]._hp -= clients[c_id]._damage;
				clients[c_id].send_attack_packet(c_id, i);
				if (clients[i]._npc_type == NT_PEACE && clients[i]._npc_attack == false) {
					clients[i]._npc_attack = true;
					clients[i]._npc_target = c_id;
				}
				if (clients[i]._hp <= 0 && clients[i]._state == ST_INGAME) {
					SC_DIE_PACKET p;
					p.id = i;
					p.size = sizeof(SC_DIE_PACKET);
					p.type = SC_DIE;
					clients[c_id].do_send(&p);
					if (clients[i]._npc_type == NT_AGRO)
						clients[c_id]._exp += clients[i]._level * clients[i]._level * 4;
					else if (clients[i]._npc_move_type == NT_ROAM)
						clients[c_id]._exp += clients[i]._level * clients[i]._level * 4;
					else
						clients[c_id]._exp += clients[i]._level * clients[i]._level * 2;
					for (int z = clients[c_id]._level; z < 10; z++) {
						if (clients[c_id]._exp >= 100 * pow(2, z - 1))clients[c_id]._level = z;
						else break;
					}
					clients[c_id].send_ingameinfo_packet();
					clients[i]._state = ST_FREE;
				}
			}
		}
		break;
	}
	case CS_ATTACK_D: {
		int x = clients[c_id].x;
		int y = clients[c_id].y;
		for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i) {
			if (true == hit_success_A(POSITION{ x,y }, POSITION{ clients[i].x,clients[i].y })) {
				clients[i]._hp -= clients[c_id]._damage * 2;
				clients[c_id].send_attack_packet(c_id, i);
				if (clients[i]._npc_type == NT_PEACE && clients[i]._npc_attack == false) {
					clients[i]._npc_attack = true;
					clients[i]._npc_target = c_id;
				}
				if (clients[i]._hp <= 0 && clients[i]._state == ST_INGAME) {
					SC_DIE_PACKET p;
					p.id = i;
					p.size = sizeof(SC_DIE_PACKET);
					p.type = SC_DIE;
					clients[c_id].do_send(&p);
					if (clients[i]._npc_type == NT_AGRO)
						clients[c_id]._exp += clients[i]._level * clients[i]._level * 4;
					else if (clients[i]._npc_move_type == NT_ROAM)
						clients[c_id]._exp += clients[i]._level * clients[i]._level * 4;
					else
						clients[c_id]._exp += clients[i]._level * clients[i]._level * 2;
					for (int z = clients[c_id]._level; z < 10; z++) {
						if (clients[c_id]._exp >= 100 * pow(2, z - 1))clients[c_id]._level = z;
						else break;
					}
					clients[c_id].send_ingameinfo_packet();
					clients[i]._state = ST_FREE;
				}
			}
		}
		break;
	}
	}
}

int get_new_client_id()
{
	for (int i = 0; i < MAX_USER; ++i) {
		std::lock_guard<std::mutex> ll{ clients[i]._s_lock };
		if (clients[i]._state == ST_FREE)
			return i;
	}
	return -1;
}

void worker_thread(HANDLE h_iocp)
{
    while (true) {
        DWORD num_bytes;
        ULONG_PTR key;
        WSAOVERLAPPED* over = nullptr;
        BOOL ret = GetQueuedCompletionStatus(h_iocp, &num_bytes, &key, &over, INFINITE);
        OVER_EXP* ex_over = reinterpret_cast<OVER_EXP*>(over);

        if (FALSE == ret) {
            if (ex_over->_comp_type == OP_ACCEPT) cout << "Accept Error";
            else {
                cout << "GQCS Error on client[" << key << "]\n";
                disconnect(static_cast<int>(key));
                if (ex_over->_comp_type == OP_SEND) delete ex_over;
                continue;
            }
        }

        if ((0 == num_bytes) && ((ex_over->_comp_type == OP_RECV) || (ex_over->_comp_type == OP_SEND))) {
            disconnect(static_cast<int>(key));
            if (ex_over->_comp_type == OP_SEND) delete ex_over;
            continue;
        }

        switch (ex_over->_comp_type) {
        case OP_ACCEPT: {
            int client_id = get_new_client_id();
            if (client_id != -1) {
                std::lock_guard<std::mutex> ll(clients[client_id]._s_lock);
                clients[client_id]._state = ST_ALLOC;
                clients[client_id].x = 0;
                clients[client_id].y = 0;
                clients[client_id]._id = client_id;
                clients[client_id]._name[0] = 0;
                clients[client_id]._prev_remain = 0;
                clients[client_id]._socket = g_c_socket;
                CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_c_socket), h_iocp, client_id, 0);
                clients[client_id].do_recv();
                g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
            }
            else {
                cout << "Max user exceeded.\n";
            }
            ZeroMemory(&g_a_over._over, sizeof(g_a_over._over));
            int addr_size = sizeof(SOCKADDR_IN);
            AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);
            break;
        }
        case OP_RECV: {
            int remain_data = num_bytes + clients[key]._prev_remain;
            char* p = ex_over->_send_buf;
            while (remain_data > sizeof(unsigned short)) {
                unsigned short packet_size = *reinterpret_cast<unsigned short*>(p);
                if (packet_size <= remain_data) {
                    process_packet(static_cast<int>(key), p);
                    p += packet_size;
                    remain_data -= packet_size;
                }
                else break;
            }
            clients[key]._prev_remain = remain_data;
            if (remain_data > 0) {
                memcpy(ex_over->_send_buf, p, remain_data);
            }
            clients[key].do_recv();
            break;
        }
        case OP_SEND:
            delete ex_over;
            break;
        case OP_NPC_MOVE: {
            // NPC 이동 로직 처리
            delete ex_over;
            break;
        }
        case OP_PLAYER_MOVE: {
            // 플레이어 이동 로직 처리
            delete ex_over;
            break;
        }
        }
    }
}
