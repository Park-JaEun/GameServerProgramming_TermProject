#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <random>
#include <queue>

#include "protocol.h"
#include "worker_thread.h"
#include "session.h"
#include "over_exp.h"

#include "include/lua.hpp"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "lua54.lib")
using namespace std;

extern vector<int> getSectorCandidates(int x, int y);

const int GRID_SIZE = 100;
const int dx[] = { -1, 1, 0, 0 };
const int dy[] = { 0, 0, -1, 1 };

// 길찾기 알고리즘에 사용할 노드 클래스
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

// Position에 대한 해시 함수 구현
struct PositionHash {
	std::size_t operator()(const POSITION& pos) const {
		return std::hash<int>()(pos.x) ^ (std::hash<int>()(pos.y) << 1);
	}
};

// 장애물과 Position 비교
struct PositionEqual {
	bool operator()(const POSITION& lhs, const POSITION& rhs) const {
		return lhs.x == rhs.x && lhs.y == rhs.y;
	}
};

bool g_obstacle_map[W_HEIGHT][W_WIDTH]; // false면 이동 가능, true면 장애물

// 장애물 생성
void generateObstacles(int count) {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> distX(0, W_WIDTH - 1);
	std::uniform_int_distribution<> distY(0, W_HEIGHT - 1);

	// 처음에 false로 초기화
	for (int y = 0; y < W_HEIGHT; y++) {
		for (int x = 0; x < W_WIDTH; x++) {
			g_obstacle_map[y][x] = false;
		}
	}

	int created = 0;
	while (created < count) {
		int ox = distX(gen);
		int oy = distY(gen);
		if (!g_obstacle_map[oy][ox]) {
			g_obstacle_map[oy][ox] = true; // 장애물
			created++;
		}
	}
}

bool isObstacle(int x, int y) {
	return g_obstacle_map[y][x];
}

HANDLE h_iocp;
array<SESSION, MAX_USER + MAX_NPC> clients;
SOCKET g_s_socket, g_c_socket;
OVER_EXP g_a_over;

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

	// 각 방향에 장애물이 없는지 체크하고 이동해야 함.
	switch (rand() % 4) {
	case 0: 
		if (x < (W_WIDTH - 1)) 	{
			x++; 
			POSITION nextPos = { x, y };
			if (false == movePossible(nextPos)) return; // break;
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
			if (false == movePossible(nextPos)) return; // break;
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
			if (false == movePossible(nextPos)) return; // break;
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
			if (false == movePossible(nextPos)) return; // break;
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
	auto candidateList2 = getSectorCandidates(npc.x, npc.y);

	for (int pid : candidateList2) {
		if (clients[pid]._state != ST_INGAME)continue;
		if (is_npc(pid))continue;
		if (can_see(npc._id, pid)) {
			new_vl.insert(pid);
			if (npc._npc_type == NT_AGRO && npc._npc_attack == false && in_npc_see(pid, npc._id)) {
				npc._npc_attack = true;
				npc._npc_target = pid;
			}
		}
	}
	for (auto pl : new_vl) {
		if (0 == old_vl.count(pl)) {
			clients[pl].send_add_player_packet(npc._id);
		}
		else {
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

				// 시야 처리, 이동 했으니까 시야 처리를 해야 함.
				{
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


int API_get_x(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);
	lua_pop(L, 2);

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

		if (clients[user_id]._hp <= 0)
		{// 죽은 경우 죽음 패킷을 보내고 경험치를 반으로 줄이고 시작 위치로 이동
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
	generateObstacles(MAX_CLOUD);
	cout << "CLOUD initialize end.\n";
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
