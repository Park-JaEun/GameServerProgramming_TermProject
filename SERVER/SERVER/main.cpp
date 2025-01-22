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
#include <queue>
#include "protocol.h"

#include "include/lua.hpp"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "lua54.lib")
using namespace std;

constexpr int VIEW_RANGE = 5;
constexpr int NPC_VIEW_RANGE = 3;

const int GRID_SIZE = 100;
const int dx[] = { -1, 1, 0, 0 };
const int dy[] = { 0, 0, -1, 1 };

vector<int>sector[SECTOR_COUNT_Y][SECTOR_COUNT_X];

// ��ã�� �˰��� ����� ��� Ŭ����
struct Node {
	int x, y;
	int g, h;
	Node* parent;
	Node(int x, int y, int g, int h, Node* parent = nullptr) : x(x), y(y), g(g), h(h), parent(parent) {}
	int f() const { return g + h; }
	bool operator<(const Node& other) const { return f() > other.f(); }
};

int heuristic(int x1, int y1, int x2, int y2) {
	return std::abs(x1 - x2) + std::abs(y1 - y2);
}

bool isValid(int x, int y) {
	return x >= 0 && x < W_WIDTH && y >= 0 && y < W_HEIGHT;
}

std::vector<Node> getPath(Node* endNode) {
	std::vector<Node> path;
	Node* current = endNode;
	while (current != nullptr) {
		path.push_back(*current);
		current = current->parent;
	}
	std::reverse(path.begin(), path.end());
	return path;
}

// Position�� ���� �ؽ� �Լ� ����
struct PositionHash {
	std::size_t operator()(const POSITION& pos) const {
		return std::hash<int>()(pos.x) ^ (std::hash<int>()(pos.y) << 1);
	}
};

// ��ֹ��� Position ��
struct PositionEqual {
	bool operator()(const POSITION& lhs, const POSITION& rhs) const {
		return lhs.x == rhs.x && lhs.y == rhs.y;
	}
};

// ��ֹ� ����
void generateObstacles(std::unordered_map<int, POSITION>& obstacles, int count) {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(0, 1000);

	for (int id = 0; obstacles.size() < count; ++id) {
		POSITION pos = { dis(gen), dis(gen) };
		obstacles[id] = pos;
	}
}

std::unordered_map<int, POSITION> obstacles;	// ��ֹ� ���

// �÷��̾� �̵� ���� ���� Ȯ�� �Լ�
bool movePossible(POSITION& player, const std::unordered_map<int, POSITION>& obstacles) {
	POSITION newPos = { player.x , player.y  };

	// �̵��Ϸ��� ��ġ�� ��ֹ����� Ȯ��
	for (const auto& obstacle : obstacles) {
		if (obstacle.second.x == newPos.x && obstacle.second.y == newPos.y) {
			//std::cout << "��ֹ��� �־� �̵��� �� �����ϴ�!" << std::endl;
			return false;
		}
	}

	//for(auto& id : list)
	//{
	//	if (obstacles[id].x == newPos.x && obstacles[id].y == newPos.y) {
	//		return false;
	//	}
	//}

	//player = newPos;
	//std::cout << "�÷��̾ (" << player.x << ", " << player.y << ")�� �̵��߽��ϴ�." << std::endl;
	return true;
}


bool isObstacle(int x, int y) {
	for (const auto& pos : obstacles) {
		if (pos.second.x == x && pos.second.y == y) return true;
	}
	return false;
}

enum EVENT_TYPE { EV_RANDOM_MOVE, EV_MOVE_TARGET};

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
class SESSION {
	OVER_EXP _recv_over;

public:
	mutex _s_lock;
	S_STATE _state;
	N_TYPE _npc_type;	// NPC�� ����
	N_TYPE _npc_move_type;	// NPC�� �̵� ���
	bool _npc_attack;	// NPC�� ���� ����
	int _npc_target;	// NPC�� ���� ���
	atomic_bool	_is_active;		// ������ �÷��̾ �ִ°�?
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
	int _damage;	// ���ݷ�
	int _hp;
	int _max_hp;
	int _level;	// ����
	int _exp;	// ����ġ
	POSITION _start_position;
	//chrono::system_clock::time_point m_npc_end_time;
	chrono::system_clock::time_point hp_time;
	std::unordered_set<int> cloud_view_list;	// ���� �� ����Ʈ
	chrono::system_clock::time_point m_npc_move_time;	// npc �̵� �ð�
	chrono::system_clock::time_point m_attack_time;	// ���� �ð�

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
		_npc_attack = false;
		_npc_target = -1;
		m_npc_move_time = chrono::system_clock::now();
		m_attack_time = chrono::system_clock::now();
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

bool in_npc_see(int from, int to)
{
	if (abs(clients[from].x - clients[to].x) > NPC_VIEW_RANGE) return false;
	return abs(clients[from].y - clients[to].y) <= NPC_VIEW_RANGE;
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
	exover->_comp_type = OP_PLAYER_MOVE;	// �÷��̾ �̵������ϱ� NPC�� �ƿ���
	exover->_ai_target_obj = waker;	// ���� �÷��̾� ����
	PostQueuedCompletionStatus(h_iocp, 1, npc_id, &exover->_over);
	clients[npc_id]._player = waker;

	if (clients[npc_id]._is_active) return;
	bool old_state = false;
	if (false == atomic_compare_exchange_strong(&clients[npc_id]._is_active, &old_state, true))
		return;
	TIMER_EVENT ev{ npc_id, chrono::system_clock::now(), EV_RANDOM_MOVE, 0 };
	timer_queue.push(ev);
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

void process_packet(int c_id, char* packet)
{
	switch (packet[2]) {
	case CS_LOGIN: {
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
		strcpy_s(clients[c_id]._name, p->name);
		{
			lock_guard<mutex> ll{ clients[c_id]._s_lock };
			clients[c_id]._state = ST_INGAME;
			clients[c_id]._npc_type = NT_PLAYER;

			// ���߿� DB���� �о�� ������ �ʱ�ȭ�� ��
			clients[c_id].x = rand() % W_WIDTH;
			clients[c_id].y = rand() % W_HEIGHT;
			//clients[c_id].x = 0;
			//clients[c_id].y = 0;
			clients[c_id]._hp = 10;
			clients[c_id]._max_hp = 10;
			clients[c_id]._damage = 1;
			clients[c_id]._level = 1;
			clients[c_id]._exp = 100;
			clients[c_id]._start_position = { clients[c_id].x, clients[c_id].y };

			clients[c_id].send_ingameinfo_packet();
			//cout << "client" << c_id << " lev : " << clients[c_id]._level << " hp : " << clients[c_id]._hp << endl;
		}
		clients[c_id].send_login_info_packet();

		// �ٸ� Ŭ���̾�Ʈ���� ���ο� Ŭ���̾�Ʈ ���� ����
		// �� Ŭ���̾�Ʈ�� ��ġ ��ó�� �ִ� Ŭ���̾�Ʈ�鿡�Ը� ���� -> ���͸� �ϸ� �� ��ġ�� ���� ���͸� �˻��ϴϱ� ������ ������ ������ ����
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

		// ��ֹ� ���� ���� ����
		// �� Ŭ���̾�Ʈ�� ��ġ ��ó�� �ִ� ���� ������ ���� -> ���͸� �ϸ� �� ��ġ�� ���� ���Ϳ� �ִ� ���� �ٷ� �����ϱ� ������ ������ ������ ����
		for (int id = 0; id < obstacles.size(); ++id) {
			if (!can_see_cloud(clients[c_id].x, clients[c_id].y, obstacles[id].x, obstacles[id].y)) continue;

			// cloud_view_list�� �߰�
			clients[c_id].cloud_view_list.insert(id);

			SC_CLOUD_PACKET p;
			p.size = sizeof(SC_CLOUD_PACKET);
			p.type = SC_CLOUD;
			p.id = id;
			p.x = obstacles[id].x;
			p.y = obstacles[id].y;
			p.in_see = true;
			clients[c_id].do_send(&p);

			//cout<< id << " " << obstacles[id].x << " " << obstacles[id].y << endl;
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
			case 0: if (y > 0) y--; break;
			case 1: if (y < W_HEIGHT - 1) y++; break;
			case 2: if (x > 0) x--; break;
			case 3: if (x < W_WIDTH - 1) x++; break;
			}

			POSITION nextPos = { x, y };
			if (false == movePossible(nextPos, obstacles)) break;
			clients[c_id].x = x;
			clients[c_id].y = y;

			unordered_set<int> near_list;
			clients[c_id]._vl.lock();
			unordered_set<int> old_vlist = clients[c_id]._view_list;
			clients[c_id]._vl.unlock();

			auto candidateList = getSectorCandidates(clients[c_id].x, clients[c_id].y);
			for (int pid : candidateList) {
				if (clients[pid]._state != ST_INGAME)continue;
				if (pid == c_id)continue;
				if (can_see(c_id, pid))near_list.insert(pid);
				if (clients[pid]._npc_type == NT_AGRO && clients[pid]._npc_attack == false && in_npc_see(c_id, pid))
				if (pid > MAX_USER) { 
						clients[pid]._npc_attack = true; clients[pid]._npc_target = c_id; 
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
				else WakeUpNPC(pl, c_id);		// NPC�� ��� �þ�ó�� ��� AI�� �����.

				if (old_vlist.count(pl) == 0)
					clients[c_id].send_add_player_packet(pl);
			}

			for (auto& pl : old_vlist)
				if (0 == near_list.count(pl)) {
					clients[c_id].send_remove_player_packet(pl);
					if (is_pc(pl))
						clients[pl].send_remove_player_packet(c_id);
				}

			// ��ֹ� ���� ���� ����
			for (int id = 0; id < obstacles.size(); ++id) {
				if (can_see_cloud(clients[c_id].x, clients[c_id].y, obstacles[id].x, obstacles[id].y))	// �þ߿� ������ �Ÿ��̸�
				{
					if (clients[c_id].cloud_view_list.find(id) == clients[c_id].cloud_view_list.end()) // cloud_view_list�� ������
					{
						clients[c_id].cloud_view_list.insert(id);	// ����

						SC_CLOUD_PACKET p;
						p.size = sizeof(SC_CLOUD_PACKET);
						p.type = SC_CLOUD;
						p.id = id;
						p.x = obstacles[id].x;
						p.y = obstacles[id].y;
						p.in_see = true;
						clients[c_id].do_send(&p);
						//cout<< id << " " << obstacles[id].x << " " << obstacles[id].y << endl;
					}
				}
				else
				{
					if (clients[c_id].cloud_view_list.find(id) != clients[c_id].cloud_view_list.end()) {	// cloud_view_list�� ������
						// cloud_view_list���� ����
						clients[c_id].cloud_view_list.erase(id);	// ����

						SC_CLOUD_PACKET p;
						p.size = sizeof(SC_CLOUD_PACKET);
						p.type = SC_CLOUD;
						p.in_see = false;
						p.id = id;
						clients[c_id].do_send(&p);
						//cout << "cloud_view_list���� ����" << endl;
					}
				}
			}
		}
		break;
	}
	case CS_CHAT: 	{
		CS_CHAT_PACKET* p = reinterpret_cast<CS_CHAT_PACKET*>(packet);
		for (auto& pl : clients) {
			if (pl._state != ST_INGAME) continue;
			if(is_pc(pl._id))
				pl.send_chat_packet(c_id, p->mess);
		}


		//cout<< "client" << c_id << " : " << p->mess << endl;
		//cout << "ssss" << endl;
	
	break;
	}

	case CS_ATTACK: {
		// �÷��̾��� ��ġ���� �����¿쿡 npc�� �ִ��� Ȯ���ϰ� ������ �����Ѵ�.
		// ������ npc�� hp�� ���ҽ�Ű��, npc�� hp�� 0�� �Ǹ� npc�� �����Ѵ�.
		if (clients[c_id].m_attack_time < chrono::system_clock::now()) {
			int x = clients[c_id].x;
			int y = clients[c_id].y;

			// ���߿� ���͸����� �þ� �ִ� npc�� �˻��ϴ� ������ �ٲ� ��
			for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i)
			{// �÷��̾� ��ǥ�� �����¿쿡 npc�� ������ npc�� hp�� ���ҽ�Ų��.
				if (true == hit_success(POSITION{ x, y }, POSITION{ clients[i].x ,clients[i].y }))
				{
					clients[c_id].m_attack_time = chrono::system_clock::now() + chrono::seconds(1);

					clients[i]._hp -= clients[c_id]._damage;
					clients[c_id].send_attack_packet(c_id, i);

					// PEACE ���ʹ� ���ݹ����� �����ڸ� �����Ѵ�.
					if (clients[i]._npc_type == NT_PEACE && clients[i]._npc_attack == false)	// ���� ����� ���� ��쿡�� ��� ����
					{
						clients[i]._npc_attack = true;	// npc ���� ���� ����, ���� ��� ����
						clients[i]._npc_target = c_id;
					}

					if (clients[i]._hp <= 0 && clients[i]._state == ST_INGAME) {
						SC_DIE_PACKET p;
						p.id = i;
						p.size = sizeof(SC_DIE_PACKET);
						p.type = SC_DIE;
						for(auto& pl : clients)
						{
							if(pl._state != ST_INGAME) continue;
							if(is_pc(pl._id))
								pl.do_send(&p);
						}
						//clients[c_id].do_send(&p);


						if (clients[i]._npc_type == NT_AGRO)
							clients[c_id]._exp += clients[i]._level * clients[i]._level * 2 * 2;
						else if (clients[i]._npc_move_type == NT_ROAM)
							clients[c_id]._exp += clients[i]._level * clients[i]._level * 2 * 2;
						else
							clients[c_id]._exp += clients[i]._level * clients[i]._level * 2;

						for (int i = clients[c_id]._level; i < 10; i++) {
							if (clients[c_id]._exp >= 100 * pow(2, i - 1)) {
								clients[c_id]._level = i;
							}
							else break;
						}

						if (clients[i]._npc_type == NT_PEACE) {
							SC_ITEM_PACKET p;
							p.id = i;
							p.size = sizeof(SC_ITEM_PACKET);
							p.type = SC_ITEM;
							clients[c_id].do_send(&p);

							clients[c_id]._hp += 10;
						}

						clients[c_id].send_ingameinfo_packet();
						//cout<< "client" << c_id << " lev : " << clients[c_id]._level << " exp : " << clients[c_id]._exp << endl;


						clients[i]._state = ST_FREE;




						//Wait30sec(i);
					}
				}
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
		clients[p->id]._npc_attack = false;
		clients[p->id]._npc_target = -1;

		break;
	}
	case CS_RECOVER:
	{
		clients[c_id]._hp += clients[c_id]._hp * 0.1;

		clients[c_id].send_ingameinfo_packet();
		//cout<< "client" << c_id << " lev : " << clients[c_id]._level << " hp : " << clients[c_id]._hp << endl;

		break;
	}
	case CS_ATTACK_A:
	{
		// �÷��̾��� ��ġ���� �����¿쿡 npc�� �ִ��� Ȯ���ϰ� ������ �����Ѵ�.
		int x = clients[c_id].x;
		int y = clients[c_id].y;

		// ���߿� ���͸����� �þ� �ִ� npc�� �˻��ϴ� ������ �ٲ� ��
		for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i)
		{// �÷��̾� ��ǥ�� �����¿쿡 npc�� ������ npc�� hp�� ���ҽ�Ų��.
			if (true == hit_success_A(POSITION{ x, y }, POSITION{ clients[i].x ,clients[i].y }))
			{
				clients[i]._hp -= clients[c_id]._damage;
				clients[c_id].send_attack_packet(c_id, i);

				// PEACE ���ʹ� ���ݹ����� �����ڸ� �����Ѵ�.
				if (clients[i]._npc_type == NT_PEACE && clients[i]._npc_attack == false)	// ���� ����� ���� ��쿡�� ��� ����
				{
					clients[i]._npc_attack = true;	// npc ���� ���� ����, ���� ��� ����
					clients[i]._npc_target = c_id;
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

					for (int i = clients[c_id]._level; i < 10; i++) {
						if (clients[c_id]._exp >= 100 * pow(2, i - 1)) {
							clients[c_id]._level = i;
						}
						else break;
					}
					clients[c_id].send_ingameinfo_packet();
					//cout<< "client" << c_id << " lev : " << clients[c_id]._level << " exp : " << clients[c_id]._exp << endl;


					clients[i]._state = ST_FREE;

					//Wait30sec(i);
				}
			}
		}

	

		break;

	}
	case CS_ATTACK_D:
	{
		// �÷��̾��� ��ġ���� �����¿쿡 npc�� �ִ��� Ȯ���ϰ� ������ �����Ѵ�.
		int x = clients[c_id].x;
		int y = clients[c_id].y;

		// ���߿� ���͸����� �þ� �ִ� npc�� �˻��ϴ� ������ �ٲ� ��
		for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i)
		{// �÷��̾� ��ǥ�� �����¿쿡 npc�� ������ npc�� hp�� ���ҽ�Ų��.
			if (true == hit_success_A(POSITION{ x, y }, POSITION{ clients[i].x ,clients[i].y }))
			{
				clients[i]._hp -= clients[c_id]._damage*2;
				clients[c_id].send_attack_packet(c_id, i);

				// PEACE ���ʹ� ���ݹ����� �����ڸ� �����Ѵ�.
				if (clients[i]._npc_type == NT_PEACE && clients[i]._npc_attack == false)	// ���� ����� ���� ��쿡�� ��� ����
				{
					clients[i]._npc_attack = true;	// npc ���� ���� ����, ���� ��� ����
					clients[i]._npc_target = c_id;
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

					for (int i = clients[c_id]._level; i < 10; i++) {
						if (clients[c_id]._exp >= 100 * pow(2, i - 1)) {
							clients[c_id]._level = i;
						}
						else break;
					}
					clients[c_id].send_ingameinfo_packet();
					//cout<< "client" << c_id << " lev : " << clients[c_id]._level << " exp : " << clients[c_id]._exp << endl;


					clients[i]._state = ST_FREE;

					//Wait30sec(i);
				}
			}
		}



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

	auto candidateList = getSectorCandidates(npc.x, npc.y);
	for (int pid : candidateList) {
		if (clients[pid]._state != ST_INGAME) continue;
		if (is_npc(pid)) continue;
		if (can_see(npc._id, pid))
			old_vl.insert(pid);
	}

	int x = npc.x;
	int y = npc.y;
		// �� ���⿡ ��ֹ��� ������ üũ�ϰ� �̵��ؾ� ��.
	switch (rand() % 4) {
	case 0: 
		if (x < (W_WIDTH - 1)) 	{
			x++; 
			POSITION nextPos = { x, y };
			if (false == movePossible(nextPos, obstacles)) return; // break;
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
			POSITION nextPos = { x, y };
			if (false == movePossible(nextPos, obstacles)) return; // break;
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
			POSITION nextPos = { x, y };
			if (false == movePossible(nextPos, obstacles)) return; // break;
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
			POSITION nextPos = { x, y };
			if (false == movePossible(nextPos, obstacles)) return; // break;
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
	for (auto& obj : clients) {	// ��Ʋ����. ���͸����� ����ȭ�ؾ� ��.
		if (ST_INGAME != obj._state) continue;
		if (true == is_npc(obj._id)) continue;
		if (true == can_see(npc._id, obj._id))
		{
			new_vl.insert(obj._id);

			if (npc._npc_type == NT_AGRO && npc._npc_attack == false && in_npc_see(obj._id, npc._id))
			{
				npc._npc_attack = true;
				npc._npc_target = obj._id;
			}
		}
	}

	for (auto pl : new_vl) {
		if (0 == old_vl.count(pl)) {
			// �÷��̾��� �þ߿� ����
			clients[pl].send_add_player_packet(npc._id);
		}
		else {
			// �÷��̾ ��� ���� ����.
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

void do_npc_goto_target(int npc_id, int target_id)
{
	SESSION& npc = clients[npc_id];
    SESSION& target = clients[target_id];

    std::priority_queue<Node> openList;
    std::unordered_map<int, bool> closedList;
    Node startNode(npc.x, npc.y, 0, heuristic(npc.x, npc.y, target.x, target.y));
    openList.push(startNode);

	unordered_set<int> old_vl;

	int min_x = min(npc.x, target.x) - 10; if (min_x < 0)min_x = 0;
	int max_x = max(npc.x, target.x) + 10; if (max_x >= W_WIDTH)max_x = W_WIDTH - 1;
	int min_y = min(npc.y, target.y) - 10; if (min_y < 0)min_y = 0;
	int max_y = max(npc.y, target.y) + 10; if (max_y >= W_HEIGHT)max_y = W_HEIGHT - 1;


	for (auto& obj : clients) {
		if (ST_INGAME != obj._state) continue;
		if (true == is_npc(obj._id)) continue;
		if (true == can_see(npc._id, obj._id))
		{
			old_vl.insert(obj._id);
		//cout<< "player in npc view" << obj._id << endl;
		//cout<< sizeof(old_vl) << endl;
		}
	}

    while (!openList.empty()) {
        Node current = openList.top();
        openList.pop();
        int index = current.x * GRID_SIZE + current.y;
        if (closedList[index]) continue;
        closedList[index] = true;

        if (current.x == target.x && current.y == target.y) {
            std::vector<Node> path = getPath(&current);
			for (size_t i = 0; i < path.size(); ++i) {
				if (npc.m_npc_move_time >= chrono::system_clock::now()) { --i; continue; }
					//std::cout << "(" << npc.x << ", " << npc.y << ")\n";
					//std::cout << "Move to (" << path[i].x << ", " << path[i].y << ")\n";
				npc.x = path[i].x;
				npc.y = path[i].y;

				npc.m_npc_move_time = chrono::system_clock::now() + chrono::seconds(1);

				// �þ� ó��, �̵� �����ϱ� �þ� ó���� �ؾ� ��.
				{
					unordered_set<int> new_vl;
					for (auto& obj : clients) {	// ��Ʋ����. ���͸����� ����ȭ�ؾ� ��.
						if (ST_INGAME != obj._state) continue;
						if (true == is_npc(obj._id)) continue;
						if (true == can_see(npc._id, obj._id))
							new_vl.insert(obj._id);
					}

					for (auto pl : new_vl) {
						if (0 == old_vl.count(pl)) {
							// �÷��̾��� �þ߿� ����
							clients[pl].send_add_player_packet(npc._id);
						}
						else {
							// �÷��̾ ��� ���� ����.
							clients[pl].send_move_packet(npc._id);
							clients[npc_id]._npc_move_time++;

							if (clients[npc_id]._npc_move_time > 3) {
								clients[npc_id]._npc_move_time = 0;
							}
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
				
			
            }
            return;
        }

        for (int i = 0; i < 4; ++i) {
            int newX = current.x + dx[i];
            int newY = current.y + dy[i];
            if (isValid(newX, newY) && !isObstacle(newX, newY)) {
				if (newX<min_x || newX>max_x || newY<min_y || newY>max_y)
					continue;

                int newG = current.g + 1;
                int newH = heuristic(newX, newY, target.x, target.y);
                Node neighbor(newX, newY, newG, newH, new Node(current));
                openList.push(neighbor);
            }
        }
    }
    //std::cout << "No path found.\n";





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
			while (remain_data > sizeof(unsigned short)) {  // �ּ� unsigned short ũ�⸦ Ȯ��
				unsigned short packet_size = *reinterpret_cast<unsigned short*>(p);  // ù 2����Ʈ ������
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

				if (clients[key]._npc_attack == true)	// ���� ��� ������
				{
					if (clients[key]._state == ST_INGAME) {
						do_npc_goto_target(static_cast<int>(key), clients[key]._npc_target);	// ���� ������� �̵�

						//cout << clients[key]._id <<" npc go to target " << clients[key]._npc_target << endl;

						TIMER_EVENT ev{ key, chrono::system_clock::now() + 1s, EV_MOVE_TARGET, 0 };
						timer_queue.push(ev);
					}

				}
				else	// ���� ��� ������ �⺻ ������
				{
					if (clients[key]._npc_move_type == NT_ROAM && clients[key]._state == ST_INGAME) {
						do_npc_random_move(static_cast<int>(key));

						TIMER_EVENT ev{ key, chrono::system_clock::now() + 1s, EV_RANDOM_MOVE, 0 };
						timer_queue.push(ev);
					
					}

				}

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
			lua_getglobal(L, "event_player_attack");	// �÷��̾ �̵���
			lua_pushnumber(L, ex_over->_ai_target_obj);	// �̵��� ��
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

	// _send_chat�� true��
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
		clients[user_id]._hp -= clients[my_id]._damage; 	// ���� �� hp ����
		clients[user_id].send_attack_packet(my_id, user_id);
		clients[user_id].send_ingameinfo_packet();

		if (clients[user_id]._hp <= 0)
		{// ���� ��� ���� ��Ŷ�� ������ ����ġ�� ������ ���̰� ���� ��ġ�� �̵�
			SC_DIE_PACKET p;
			p.id = user_id;
			p.size = sizeof(SC_DIE_PACKET);
			p.type = SC_DIE;
			clients[user_id].do_send(&p);

			clients[user_id]._exp = clients[user_id]._exp * 0.5;
			clients[user_id]._hp = clients[user_id]._max_hp;
			clients[user_id].x = clients[user_id]._start_position.x;
			clients[user_id].y = clients[user_id]._start_position.y;
			clients[user_id].send_ingameinfo_packet();
			clients[user_id].send_move_packet(user_id);
			//cout<< "client" << my_id << " lev : " << clients[my_id]._level << " exp : " << clients[my_id]._exp << endl;

		//cout << "user_id : " << user_id << " hp : " << clients[user_id]._hp << endl;
		}

		return 0;
	}
}

void InitializeNPC()
{
	cout << "NPC intialize begin.\n";
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
			//clients[i]._npc_move_type = NT_FIX;
		}
		else {
			clients[i]._hp = 5;
			clients[i]._max_hp = 5;
			clients[i]._damage = 1;
			clients[i]._npc_type = NT_AGRO;
			//clients[i]._npc_type = NT_PEACE;
			clients[i]._level = 10;
			if (rand() % 2 == 0) clients[i]._npc_move_type = NT_FIX;
			else clients[i]._npc_move_type = NT_ROAM;
			//clients[i]._npc_move_type = NT_FIX;
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
	// ��ֹ��� cloud�� �ʿ� �������� ��ġ��.
	// cloud�� npc�� �÷��̾ �̵��� �� ������ �ϴ� ��ֹ���.
	// cloud�� �̵����� ����.
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
				timer_queue.push(ev);		// ����ȭ �ʿ�
				// timer_queue�� �ٽ� ���� �ʰ� ó���ؾ� �Ѵ�.
				this_thread::sleep_for(1ms);  // ����ð��� ���� �ȵǾ����Ƿ� ��� ���
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
			/*case EV_MOVE_TARGET:
			{
				OVER_EXP* ov = new OVER_EXP;
				ov->_comp_type = OP_NPC_MOVE;
				ov->_ai_target_obj = clients[ev.obj_id]._player;
				PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ov->_over);
				break;
			}*/
			}
			continue;		// ��� ���� �۾� ������
		}
		this_thread::sleep_for(1ms);   // timer_queue�� ��� ������ ��� ��ٷȴٰ� �ٽ� ����
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
