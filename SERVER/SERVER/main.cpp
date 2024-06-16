#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <concurrent_priority_queue.h>
#include <random>
#include "protocol.h"

#include "include/lua.hpp"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "lua54.lib")
using namespace std;

constexpr int VIEW_RANGE = 5;

// Position에 대한 해시 함수 구현
struct PositionHash {
	std::size_t operator()(const POSITION& pos) const {
		return std::hash<int>()(pos.x) ^ (std::hash<int>()(pos.y) << 1);
	}
};

// Position 비교
struct PositionEqual {
	bool operator()(const POSITION& lhs, const POSITION& rhs) const {
		return lhs.x == rhs.x && lhs.y == rhs.y;
	}
};

// 장애물 생성
//void generateObstacles(std::unordered_set<POSTION, PositionHash, PositionEqual>& obstacles, int count) {
//	std::random_device rd;
//	std::mt19937 gen(rd());
//	std::uniform_int_distribution<> dis(0, 2000);
//
//	while (obstacles.size() < count) {
//		POSTION pos = { dis(gen), dis(gen) };
//		obstacles.insert(pos);
//	}
//}
void generateObstacles(std::unordered_map<int, POSITION>& obstacles, int count) {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(0, 1000);

	for (int id = 0; obstacles.size() < count; ++id) {
		POSITION pos = { dis(gen), dis(gen) };
		obstacles[id] = pos;
	}
}

// 플레이어 이동 가능 여부 확인 함수
//bool movePlayer(POSTION& player, int dx, int dy, const std::unordered_set<POSTION, PositionHash, PositionEqual>& obstacles) {
//	POSTION newPos = { player.x + dx, player.y + dy };
//
//	if (obstacles.find(newPos) != obstacles.end()) {
//		std::cout << "장애물이 있어 이동할 수 없습니다!" << std::endl;
//		return false;
//	}
//
//	//std::cout << "플레이어가 (" << player.x << ", " << player.y << ")로 이동했습니다." << std::endl;
//	return true;
//}
bool movePlayer(POSITION& player, const std::unordered_map<int, POSITION>& obstacles) {
	POSITION newPos = { player.x , player.y  };

	// 이동하려는 위치가 장애물인지 확인
	for (const auto& obstacle : obstacles) {
		if (obstacle.second.x == newPos.x && obstacle.second.y == newPos.y) {
			std::cout << "장애물이 있어 이동할 수 없습니다!" << std::endl;
			return false;
		}
	}

	//player = newPos;
	//std::cout << "플레이어가 (" << player.x << ", " << player.y << ")로 이동했습니다." << std::endl;
	return true;
}


//std::unordered_set<POSTION, PositionHash, PositionEqual> obstacles;	// 장애물 목록
std::unordered_map<int, POSITION> obstacles;	// 장애물 목록

enum EVENT_TYPE { EV_RANDOM_MOVE };

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

enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_NPC_MOVE, OP_PLAYER_MOVE };
class OVER_EXP {
public:
	WSAOVERLAPPED _over;
	WSABUF _wsabuf;
	char _send_buf[BUF_SIZE];
	COMP_TYPE _comp_type;
	int _ai_target_obj;
	OVER_EXP()
	{
		_wsabuf.len = BUF_SIZE;
		_wsabuf.buf = _send_buf;
		_comp_type = OP_RECV;
		ZeroMemory(&_over, sizeof(_over));
	}
	OVER_EXP(char* packet)
	{
		_wsabuf.len = packet[0];
		_wsabuf.buf = _send_buf;
		ZeroMemory(&_over, sizeof(_over));
		_comp_type = OP_SEND;
		memcpy(_send_buf, packet, packet[0]);
	}
};

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };
//enum N_TYPE { NT_PEACE, NT_AGRO, NT_PLAYER };
class SESSION {
	OVER_EXP _recv_over;

public:
	mutex _s_lock;
	S_STATE _state;
	N_TYPE _npc_type;	// NPC의 종류
	N_TYPE _npc_move_type;	// NPC의 이동 방식
	atomic_bool	_is_active;		// 주위에 플레이어가 있는가?
	int _id;
	SOCKET _socket;
	short	x, y;
	char	_name[NAME_SIZE];
	int		_prev_remain;
	unordered_set <int> _view_list;
	mutex	_vl;
	int		last_move_time;
	lua_State*	_L;
	mutex	_ll;
	int _npc_move_time;
	bool _send_chat;
	int _player;
	int _damage;	// 공격력
	int _hp;
	int _max_hp;
	int _level;	// 레벨
	int _exp;	// 경험치
	//chrono::system_clock::time_point m_npc_end_time;
	chrono::system_clock::time_point hp_time;
public:
	SESSION()
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
	}

	~SESSION() {}

	void do_recv()
	{
		DWORD recv_flag = 0;
		memset(&_recv_over._over, 0, sizeof(_recv_over._over));
		_recv_over._wsabuf.len = BUF_SIZE - _prev_remain;
		_recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain;
		WSARecv(_socket, &_recv_over._wsabuf, 1, 0, &recv_flag,
			&_recv_over._over, 0);
	}

	void do_send(void* packet)
	{
		OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
		WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, &sdata->_over, 0);
	}
	void send_login_info_packet()
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
	void send_move_packet(int c_id);
	void send_add_player_packet(int c_id);
	void send_chat_packet(int c_id, const char* mess);
	void send_attack_packet(int a_id, int t_id);
	void send_ingameinfo_packet();
	void send_remove_player_packet(int c_id)
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
};

HANDLE h_iocp;
array<SESSION, MAX_USER + MAX_NPC> clients;
SOCKET g_s_socket, g_c_socket;
OVER_EXP g_a_over;

bool is_pc(int object_id)
{
	return object_id < MAX_USER;
}

bool is_npc(int object_id)
{
	return !is_pc(object_id);
}

bool can_see(int from, int to)
{
	if (abs(clients[from].x - clients[to].x) > VIEW_RANGE) return false;
	return abs(clients[from].y - clients[to].y) <= VIEW_RANGE;
}

bool can_see_cloud(int c_x, int c_y, int cloud_x, int cloud_y)
{
	if (abs(c_x - cloud_x) > VIEW_RANGE) return false;
	return abs(c_y - cloud_y) <= VIEW_RANGE;
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
	if (is_npc(c_id)) add_packet.npc_type = clients[c_id]._npc_type;
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

int get_new_client_id()
{
	for (int i = 0; i < MAX_USER; ++i) {
		lock_guard <mutex> ll{ clients[i]._s_lock };
		if (clients[i]._state == ST_FREE)
			return i;
	}
	return -1;
}

void WakeUpNPC(int npc_id, int waker)
{
	OVER_EXP* exover = new OVER_EXP;
	exover->_comp_type = OP_PLAYER_MOVE;	// 플레이어가 이동했으니까 NPC를 꺠워라
	exover->_ai_target_obj = waker;	// 깨운 플레이어 정보
	PostQueuedCompletionStatus(h_iocp, 1, npc_id, &exover->_over);
	clients[npc_id]._player = waker;

	if (clients[npc_id]._is_active) return;
	bool old_state = false;
	if (false == atomic_compare_exchange_strong(&clients[npc_id]._is_active, &old_state, true))
		return;
	TIMER_EVENT ev{ npc_id, chrono::system_clock::now(), EV_RANDOM_MOVE, 0 };
	timer_queue.push(ev);
}

std::unordered_set<int> cloud_view_list;	// 클라별로 구름 뷰 리스트를 가지고 있도록 수정하기

void process_packet(int c_id, char* packet)
{
	// 구름 뷰 리스트 생성

	switch (packet[2]) {
	case CS_LOGIN: {
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
		strcpy_s(clients[c_id]._name, p->name);
		{
			lock_guard<mutex> ll{ clients[c_id]._s_lock };
			clients[c_id].x = rand() % W_WIDTH;
			clients[c_id].y = rand() % W_HEIGHT;
			clients[c_id]._state = ST_INGAME;
			clients[c_id]._npc_type = NT_PLAYER;

			// 나중에 DB에서 읽어온 정보로 초기화할 것
			clients[c_id]._hp = 10;
			clients[c_id]._max_hp = 10;
			clients[c_id]._damage = 1;
			clients[c_id]._level = 1;
			clients[c_id]._exp = 100;

			//cout << "client" << c_id << " lev : " << clients[c_id]._level << " hp : " << clients[c_id]._hp << endl;


		}
		clients[c_id].send_login_info_packet();
		for (auto& pl : clients) {
			{
				lock_guard<mutex> ll(pl._s_lock);
				if (ST_INGAME != pl._state) continue;
			}
			if (pl._id == c_id) continue;
			if (false == can_see(c_id, pl._id))
				continue;
			if (is_pc(pl._id)) pl.send_add_player_packet(c_id);
			else WakeUpNPC(pl._id, c_id);
			clients[c_id].send_add_player_packet(pl._id);
		}

		int count = 0;
		//for(auto& obs : obstacles) {	// 장애물 구름 정보 전송

		// 장애물 구름 정보 전송
		for (int id = 0; id < obstacles.size() ; ++id) {
			if(!can_see_cloud(clients[c_id].x, clients[c_id].y, obstacles[id].x, obstacles[id].y)) continue;

			// cloud_view_list에 추가
			cloud_view_list.insert(id);

			SC_CLOUD_PACKET p;
			p.size = sizeof(SC_CLOUD_PACKET);
			p.type = SC_CLOUD;
			p.id = id;
			p.x = obstacles[id].x;
			p.y = obstacles[id].y;
			clients[c_id].do_send(&p);

			cout << id << " " << obstacles[id].x << " " << obstacles[id].y << endl;


			//cout<< id << " " << obstacles[id].x << " " << obstacles[id].y << endl;
		}


		break;
	}
	case CS_MOVE: {
		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
		clients[c_id].last_move_time = p->move_time;
		short x = clients[c_id].x;
		short y = clients[c_id].y;
		switch (p->direction) {
		case 0: if (y > 0) y--; break;
		case 1: if (y < W_HEIGHT - 1) y++; break;
		case 2: if (x > 0) x--; break;
		case 3: if (x < W_WIDTH - 1) x++; break;
		}

		POSITION nextPos = { x, y };
		if (false == movePlayer(nextPos, obstacles)) break;
		clients[c_id].x = x;
		clients[c_id].y = y;

		unordered_set<int> near_list;
		clients[c_id]._vl.lock();
		unordered_set<int> old_vlist = clients[c_id]._view_list;
		clients[c_id]._vl.unlock();
		for (auto& cl : clients) {
			if (cl._state != ST_INGAME) continue;
			if (cl._id == c_id) continue;
			if (can_see(c_id, cl._id))
				near_list.insert(cl._id);
		}

		clients[c_id].send_move_packet(c_id);

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
			else WakeUpNPC(pl, c_id);		// NPC일 경우 시야처리 대신 AI를 깨운다.

			if (old_vlist.count(pl) == 0)
				clients[c_id].send_add_player_packet(pl);
		}

		for (auto& pl : old_vlist)
			if (0 == near_list.count(pl)) {
				clients[c_id].send_remove_player_packet(pl);
				if (is_pc(pl))
					clients[pl].send_remove_player_packet(c_id);
			}

		// 장애물 구름 정보 전송
		for (int id = 0; id < obstacles.size(); ++id) {

			//if (!can_see_cloud(clients[c_id].x, clients[c_id].y, obstacles[id].x, obstacles[id].y)) {
			//	// cloud_view_list에 있는지 확인
			//	if (cloud_view_list.count(id)) {
			//		// cloud_view_list에서 제거
			//		cloud_view_list.erase(id);

			//		SC_CLOUD_PACKET p;
			//		p.size = sizeof(SC_CLOUD_PACKET);
			//		p.type = SC_CLOUD;
			//		p.in_see = false;
			//		p.id = id;
			//		clients[c_id].do_send(&p);

			//		cout << "cloud_view_list에서 제거" << endl;
			//	}

			//	continue;
			//}
			//else 
			if (can_see_cloud(clients[c_id].x, clients[c_id].y, obstacles[id].x, obstacles[id].y))
			{
				if (cloud_view_list.find(id) == cloud_view_list.end()) {
					SC_CLOUD_PACKET p;
					p.size = sizeof(SC_CLOUD_PACKET);
					p.type = SC_CLOUD;
					p.id = id;
					p.x = obstacles[id].x;
					p.y = obstacles[id].y;
					p.in_see = true;
					clients[c_id].do_send(&p);

					cloud_view_list.insert(id);
					cout<< id << " " << obstacles[id].x << " " << obstacles[id].y << endl;
				}
			}
			else
			{
				if (cloud_view_list.find(id) != cloud_view_list.end()) {
					// cloud_view_list에서 제거
					cloud_view_list.erase(id);

					SC_CLOUD_PACKET p;
					p.size = sizeof(SC_CLOUD_PACKET);
					p.type = SC_CLOUD;
					p.in_see = false;
					p.id = id;
					clients[c_id].do_send(&p);

					cout << "cloud_view_list에서 제거" << endl;
				}


			}

		}

		break;
	}

	case CS_ATTACK:	{
		// 플레이어의 위치에서 상하좌우에 npc가 있는지 확인하고 있으면 공격한다.
		// 공격은 npc의 hp를 감소시키고, npc의 hp가 0이 되면 npc를 제거한다.
		int x = clients[c_id].x;
		int y = clients[c_id].y;

		// 나중에 섹터링으로 시야 있는 npc만 검사하는 것으로 바꿀 것
		for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i)
		{// 플레이어 좌표의 상하좌우에 npc가 있으면 npc의 hp를 감소시킨다.
			if (clients[i].x == x-1 && clients[i].y == y) 
			{
				clients[i]._hp -= clients[c_id]._damage;
				clients[c_id].send_attack_packet(c_id, i);

				cout << "npc_id : " << i << " hp : " << clients[i]._hp << endl;
			}
			if (clients[i].x == x && clients[i].y == y-1)
			{
				clients[i]._hp -= clients[c_id]._damage;
				clients[c_id].send_attack_packet(c_id, i);

				cout << "npc_id : " << i << " hp : " << clients[i]._hp << endl;
			}
			if (clients[i].x == x+1 && clients[i].y == y)
			{
				clients[i]._hp -= clients[c_id]._damage;
				clients[c_id].send_attack_packet(c_id, i);

				cout << "npc_id : " << i << " hp : " << clients[i]._hp << endl;
			}
			if (clients[i].x == x && clients[i].y == y+1)
			{
				clients[i]._hp -= clients[c_id]._damage;
				clients[c_id].send_attack_packet(c_id, i);
				cout << "npc_id : " << i << " hp : " << clients[i]._hp << endl;
			}

			if (clients[i]._hp <= 0 && clients[i]._state == ST_INGAME) {
				SC_DIE_PACKET p;
				p.id = i;
				p.size = sizeof(SC_DIE_PACKET);
				p.type = SC_DIE;
				clients[c_id].do_send(&p);


				if (clients[i]._npc_type == NT_AGRO)
					clients[c_id]._exp += clients[i]._level * clients[i]._level * 2 * 2;
				else if (clients[i]._npc_move_type == NT_ROAM)
					clients[c_id]._exp += clients[i]._level * clients[i]._level * 2 * 2;
				else
					clients[c_id]._exp += clients[i]._level * clients[i]._level * 2;

				for(int i = clients[c_id]._level ; i< 10 ; i++) {
					if (clients[c_id]._exp >= 100 * pow(2, i - 1)) {
						clients[c_id]._level = i;


					}else break;
				}
				clients[c_id].send_ingameinfo_packet();
				//cout<< "client" << c_id << " lev : " << clients[c_id]._level << " exp : " << clients[c_id]._exp << endl;


				clients[i]._state = ST_FREE;
				//Wait30sec(i);
			}
		}
		break;
	}
	case CS_NPC_WAKED:
	{
		CS_NPC_WAKED_PACKET* p = reinterpret_cast<CS_NPC_WAKED_PACKET*>(packet);
		//WakeUpNPC(p->id, c_id);
		clients[p->id]._state = ST_INGAME;
		clients[p->id]._hp = clients[p->id]._max_hp;

		break;
	}

	case CS_RECOVER:
	{
		clients[c_id]._hp += clients[c_id]._hp * 0.1;

		clients[c_id].send_ingameinfo_packet();
		//cout<< "client" << c_id << " lev : " << clients[c_id]._level << " hp : " << clients[c_id]._hp << endl;

		break;

	}

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
			lock_guard<mutex> ll(pl._s_lock);
			if (ST_INGAME != pl._state) continue;
		}
		if (pl._id == c_id) continue;
		pl.send_remove_player_packet(c_id);
	}
	closesocket(clients[c_id]._socket);

	lock_guard<mutex> ll(clients[c_id]._s_lock);
	clients[c_id]._state = ST_FREE;
}

void do_npc_random_move(int npc_id)
{
	SESSION& npc = clients[npc_id];
	unordered_set<int> old_vl;
	for (auto& obj : clients) {
		if (ST_INGAME != obj._state) continue;
		if (true == is_npc(obj._id)) continue;
		if (true == can_see(npc._id, obj._id))
			old_vl.insert(obj._id);
	}

	int x = npc.x;
	int y = npc.y;
		// 각 방향에 장애물이 없는지 체크하고 이동해야 함.
	switch (rand() % 4) {
	case 0: 
		if (x < (W_WIDTH - 1)) 	{
			x++; 
			if (clients[npc_id]._send_chat == true) {
				clients[npc_id]._npc_move_time++;

				if (clients[npc_id]._npc_move_time > 3) {
					clients[npc_id]._send_chat = false;
					clients[npc_id]._npc_move_time = 0;
				}
			}
		}break;
	case 1: 
		if (x > 0)	{
			x--;
			if (clients[npc_id]._send_chat == true) {
				clients[npc_id]._npc_move_time++;

				if (clients[npc_id]._npc_move_time > 3) {
					clients[npc_id]._send_chat = false;
					clients[npc_id]._npc_move_time = 0;
				}
			}
		}break;
	case 2: 
		if (y < (W_HEIGHT - 1))	{
			y++;
			if (clients[npc_id]._send_chat == true) {
				clients[npc_id]._npc_move_time++;

				if (clients[npc_id]._npc_move_time > 3) {
					clients[npc_id]._send_chat = false;
					clients[npc_id]._npc_move_time = 0;
				}
			}
		}break;
	case 3:
		if (y > 0)	{
			y--;
			if (clients[npc_id]._send_chat == true) {
				clients[npc_id]._npc_move_time++;

				if (clients[npc_id]._npc_move_time > 3) {
					clients[npc_id]._send_chat = false;
					clients[npc_id]._npc_move_time = 0;
				}
			}
		}break;
	}
	npc.x = x;
	npc.y = y;


	unordered_set<int> new_vl;
	for (auto& obj : clients) {	// 보틀넥임. 섹터링으로 최적화해야 함.
		if (ST_INGAME != obj._state) continue;
		if (true == is_npc(obj._id)) continue;
		if (true == can_see(npc._id, obj._id))
			new_vl.insert(obj._id);
	}

	for (auto pl : new_vl) {
		if (0 == old_vl.count(pl)) {
			// 플레이어의 시야에 등장
			clients[pl].send_add_player_packet(npc._id);
		}
		else {
			// 플레이어가 계속 보고 있음.
			clients[pl].send_move_packet(npc._id);
		}
	}
	
	for (auto pl : old_vl) {
		if (0 == new_vl.count(pl)) {
			clients[pl]._vl.lock();
			if (0 != clients[pl]._view_list.count(npc._id)) {
				clients[pl]._vl.unlock();
				clients[pl].send_remove_player_packet(npc._id);
			}
			else {
				clients[pl]._vl.unlock();
			}
		}
	}
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
				{
					lock_guard<mutex> ll(clients[client_id]._s_lock);
					clients[client_id]._state = ST_ALLOC;
				}
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
			while (remain_data > sizeof(unsigned short)) {  // 최소 unsigned short 크기를 확인
				unsigned short packet_size = *reinterpret_cast<unsigned short*>(p);  // 첫 2바이트 사이즈
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
			bool keep_alive = false;
			for (int j = 0; j < MAX_USER; ++j) {
				if (clients[j]._state != ST_INGAME) continue;
				if (can_see(static_cast<int>(key), j)) {
					keep_alive = true;
					break;
				}
			}
			if (true == keep_alive) {
				if (clients[key]._npc_move_type == NT_ROAM && clients[key]._state == ST_INGAME)
					do_npc_random_move(static_cast<int>(key));

				TIMER_EVENT ev{ key, chrono::system_clock::now() + 1s, EV_RANDOM_MOVE, 0 };
				timer_queue.push(ev);
				{
					clients[key]._ll.lock();
					auto L = clients[key]._L;
					lua_getglobal(L, "event_player_attack");	
					lua_pushnumber(L, ex_over->_ai_target_obj);	
					lua_pcall(L, 1, 0, 0);
					clients[key]._ll.unlock();
				}
			}
			else {
				clients[key]._is_active = false;
			}
			delete ex_over;
		}
			break;
		case OP_PLAYER_MOVE: {
			clients[key]._ll.lock();
			auto L = clients[key]._L;
			lua_getglobal(L, "event_player_attack");	// 플레이어가 이동함
			lua_pushnumber(L, ex_over->_ai_target_obj);	// 이동한 애
			lua_pcall(L, 1, 0, 0);
			clients[key]._ll.unlock();
			delete ex_over;
		}
			break;

		}
	}
}

int API_get_x(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);
	lua_pop(L, 2);
	//if (!is_pc(user_id))
	//{
	//	lua_pushnumber(L, -1);
	//	return 1;
	//}
	if (user_id >= 0)
	{
		int x = clients[user_id].x;
		lua_pushnumber(L, x);
	}
	
	return 1;
}

int API_get_y(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);
	lua_pop(L, 2);
	//if (!is_pc(user_id))
	//{
	//	lua_pushnumber(L, -1);
	//	return 1;
	//}
	if (user_id >= 0)
	{
		int y = clients[user_id].y;
		lua_pushnumber(L, y);
	}
	return 1;
}

int API_get_npc_move_time(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);
	lua_pop(L, 2);
	//if (!is_pc(user_id))
	//{
	//	lua_pushnumber(L, -1);
	//	return 1;
	//}
	if (user_id >= 0)
	{
		int npc_move_time = clients[user_id]._npc_move_time;
		//cout << npc_move_time << endl;
		lua_pushnumber(L, npc_move_time);
	}
	return 1;
}

int API_SendMessage(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -3);
	int user_id = (int)lua_tointeger(L, -2);
	char* mess = (char*)lua_tostring(L, -1);

	lua_pop(L, 4);

	clients[user_id].send_chat_packet(my_id, mess);

	// _send_chat을 true로
	lock_guard <mutex> ll{ clients[my_id]._s_lock };
	clients[my_id]._send_chat = true;

	return 0;
}

int API_Attack(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -2);
	int user_id = (int)lua_tointeger(L, -1);

	lua_pop(L, 3);

	if (clients[my_id]._state == ST_INGAME) {
		clients[user_id]._hp -= clients[my_id]._damage; 	// 맞은 애 hp 감소
		clients[user_id].send_attack_packet(my_id, user_id);
		clients[user_id].send_ingameinfo_packet();
		//cout << "user_id : " << user_id << " hp : " << clients[user_id]._hp << endl;
	}

	return 0;
}

void InitializeNPC()
{
	cout << "NPC intialize begin.\n";
	//for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i) {
	for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i) {
		clients[i].x = rand() % W_WIDTH;
		clients[i].y = rand() % W_HEIGHT;
		clients[i]._id = i;
		sprintf_s(clients[i]._name, "NPC%d", i);
		clients[i]._state = ST_INGAME;
		if (rand() % 2 == 0) {
			clients[i]._hp = 3;
			clients[i]._max_hp = 3;
			clients[i]._damage = 1;
			clients[i]._npc_type = NT_PEACE;
			clients[i]._level = 5;
			if (rand() % 2 == 0) clients[i]._npc_move_type = NT_FIX;
			else clients[i]._npc_move_type = NT_ROAM;
		}
		else {
			clients[i]._hp = 5;
			clients[i]._max_hp = 5;
			clients[i]._damage = 1;
			clients[i]._npc_type = NT_AGRO;
			clients[i]._level = 10;
			if (rand() % 2 == 0) clients[i]._npc_move_type = NT_FIX;
			else clients[i]._npc_move_type = NT_ROAM;
		}


		auto L = clients[i]._L = luaL_newstate();
		luaL_openlibs(L);
		luaL_loadfile(L, "npc.lua");
		lua_pcall(L, 0, 0, 0);

		lua_getglobal(L, "set_uid");
		lua_pushnumber(L, i);
		lua_pcall(L, 1, 0, 0);
		// lua_pop(L, 1);// eliminate set_uid from stack after call

		lua_register(L, "API_SendMessage", API_SendMessage);
		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
		lua_register(L, "API_get_npc_move_time", API_get_npc_move_time);
		lua_register(L, "API_Attack", API_Attack);
	}
	cout << "NPC initialize end.\n";
}

void InitializeCloud()
{
	// 장애물인 cloud를 맵에 랜덤으로 배치함.
	// cloud는 npc와 플레이어가 이동할 수 없도록 하는 장애물임.
	// cloud는 이동하지 않음.
	cout << "CLOUD intialize begin.\n";
	generateObstacles(obstacles, MAX_CLOUD);
	cout << "CLOUD initialize end.\n";
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
				OVER_EXP* ov = new OVER_EXP;
				ov->_comp_type = OP_NPC_MOVE;
				ov->_ai_target_obj = clients[ev.obj_id]._player;
				PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ov->_over);
				break;
			}
			continue;		// 즉시 다음 작업 꺼내기
		}
		this_thread::sleep_for(1ms);   // timer_queue가 비어 있으니 잠시 기다렸다가 다시 시작
	}
}

int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	g_s_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUM);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(g_s_socket, SOMAXCONN);
	SOCKADDR_IN cl_addr;
	int addr_size = sizeof(cl_addr);

	InitializeNPC();
	InitializeCloud();

	h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), h_iocp, 9999, 0);
	g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	g_a_over._comp_type = OP_ACCEPT;
	AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);

	vector <thread> worker_threads;
	int num_threads = std::thread::hardware_concurrency();
	for (int i = 0; i < num_threads; ++i)
		worker_threads.emplace_back(worker_thread, h_iocp);
	thread timer_thread{ do_timer };
	timer_thread.join();
	for (auto& th : worker_threads)
		th.join();
	closesocket(g_s_socket);
	WSACleanup();
}
