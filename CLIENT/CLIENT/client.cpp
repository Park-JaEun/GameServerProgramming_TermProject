#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <unordered_map>
#include <Windows.h>
#include <chrono>
#include <thread>

using namespace std;
using namespace std::chrono;

#include "..\..\SERVER\SERVER\protocol.h"

sf::TcpSocket s_socket;

constexpr auto SCREEN_WIDTH = 16;
constexpr auto SCREEN_HEIGHT = 16;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = SCREEN_WIDTH * TILE_WIDTH;   // size of window
constexpr auto WINDOW_HEIGHT = SCREEN_WIDTH * TILE_WIDTH;

bool is_pc(int object_id)
{
	return object_id < MAX_USER;
}
bool is_npc(int object_id)
{
	return !is_pc(object_id);
}
void send_packet(void* packet)
{
	unsigned char* p = reinterpret_cast<unsigned char*>(packet);
	size_t sent = 0;
	s_socket.send(packet, p[0], sent);
}

int g_left_x;
int g_top_y;
int g_myid;

sf::RenderWindow* g_window;
sf::Font g_font;

class OBJECT {
private:
	bool m_showing;
	bool m_alive;
	sf::Sprite m_sprite;

	sf::Text m_name;
	sf::Text m_chat;
	chrono::system_clock::time_point m_mess_end_time;
	chrono::system_clock::time_point m_attack_end_time;
	chrono::system_clock::time_point m_npc_end_time;
	chrono::system_clock::time_point m_recover_time;
public:
	int id;
	int m_x, m_y;
	char name[NAME_SIZE];
	char nickname[NAME_SIZE];
	int level;
	int hp;
	int exp;
	int damage;
	N_TYPE npc_type;
	N_TYPE npc_move_type;

	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_showing = false;
		m_alive = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		set_name("NONAME");
		m_mess_end_time = chrono::system_clock::now();
		m_recover_time = chrono::system_clock::now() + chrono::seconds(5);
	}
	OBJECT() {
		m_showing = false;
	}
	void show()
	{
		m_showing = true;
		m_attack_end_time = chrono::system_clock::now() + chrono::seconds(1);
	}

	void check_recover() {
		if (m_recover_time < chrono::system_clock::now()) {
			CS_RECOVER_PACKET p;
			p.size = sizeof(CS_RECOVER_PACKET);
			p.type = CS_RECOVER;
			send_packet(&p);

			m_recover_time = chrono::system_clock::now() + chrono::seconds(5);
		}
	}

	void set_endtime() {
		m_npc_end_time = chrono::system_clock::now() + chrono::seconds(3);
	}

	void check_endtime() {
		if (true == m_showing) return;
		if (m_npc_end_time < chrono::system_clock::now()) {
			show();

			{
				CS_NPC_WAKED_PACKET p;
				p.size = sizeof(CS_NPC_WAKED_PACKET);
				p.type = CS_NPC_WAKED;
				p.id = id;
				send_packet(&p);
			}
		}
	}

	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_left_x) * 65.0f + 1;
		float ry = (m_y - g_top_y) * 65.0f + 1;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		auto size = m_name.getGlobalBounds();
		if (m_mess_end_time < chrono::system_clock::now()) {
			m_name.setPosition(rx + 32 - size.width / 2, ry - 10);
			g_window->draw(m_name);
		}
		else {
			m_chat.setPosition(rx + 32 - size.width / 2, ry - 10);
			g_window->draw(m_chat);
		}
		
	}
	void attack_draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_left_x) * 65.0f + 1;
		float ry = (m_y - g_top_y) * 65.0f + 1;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		//cout << "attack\n";
		if (m_attack_end_time < chrono::system_clock::now()) {
			//cout << "attack done\n";
			m_showing = false;
		}
	}
	void set_name(const char str[]) {
		m_name.setFont(g_font);
		m_name.setString(str);
		if (id < MAX_USER) m_name.setFillColor(sf::Color(255, 255, 255));
		else m_name.setFillColor(sf::Color(255, 255, 0));
		m_name.setStyle(sf::Text::Bold);
	}

	void set_chat(const char str[]) {
		m_chat.setFont(g_font);
		m_chat.setString(str);
		m_chat.setFillColor(sf::Color(255, 255, 255));
		m_chat.setStyle(sf::Text::Bold);
		m_mess_end_time = chrono::system_clock::now() + chrono::seconds(3);
	}
};

OBJECT avatar;
unordered_map <int, OBJECT> players;

OBJECT blue_tile;
OBJECT green_tile;
OBJECT P_A_U, P_A_D, P_A_L, P_A_R;

sf::Texture* board;
sf::Texture* player;
sf::Texture* pieces_m1;
sf::Texture* pieces_m2;
sf::Texture* pieces_m3;
sf::Texture* p_attack;

void client_initialize()
{
	board = new sf::Texture;
	player = new sf::Texture;
	pieces_m1 = new sf::Texture;
	pieces_m2 = new sf::Texture;
	pieces_m3 = new sf::Texture;
	p_attack = new sf::Texture;
	board->loadFromFile("mymap.bmp");
	player->loadFromFile("c_idle.png");
	p_attack->loadFromFile("c_attack.png");
	//pieces->loadFromFile("cloud.png");	// 장애물 구름
	//pieces->loadFromFile("n_on.png");	// npc
	pieces_m1->loadFromFile("m1_idle.png");	// lv1 몬스터
	pieces_m2->loadFromFile("m2_idle.png");	// lv2 몬스터
	pieces_m3->loadFromFile("m_boss_idle.png");		// 보스
	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		exit(-1);
	}
	blue_tile = OBJECT{ *board, 0, 0, TILE_WIDTH, TILE_WIDTH };
	green_tile = OBJECT{ *board, 64, 0, TILE_WIDTH, TILE_WIDTH };
	avatar = OBJECT{ *player, 0, 0, 64, 64 };
	P_A_U = OBJECT{ *p_attack, 0, 0, 64, 64 };
	P_A_D = OBJECT{ *p_attack, 0, 0, 64, 64 };
	P_A_L = OBJECT{ *p_attack, 0, 0, 64, 64 };
	P_A_R = OBJECT{ *p_attack, 0, 0, 64, 64 };

	avatar.move(4, 4);
}

void client_finish()
{
	players.clear();
	delete board;
	delete player;
	delete pieces_m1;
	delete pieces_m2;
	delete pieces_m3;
	delete p_attack;
}

void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[2])
	{
	case SC_LOGIN_INFO:
	{
		//cout << "SC_LOGIN_INFO\n";
		SC_LOGIN_INFO_PACKET * packet = reinterpret_cast<SC_LOGIN_INFO_PACKET*>(ptr);
		g_myid = packet->id;
		avatar.id = g_myid;
		P_A_U.id = g_myid; 	P_A_D.id = g_myid;	P_A_L.id = g_myid;	P_A_R.id = g_myid;
		avatar.move(packet->x, packet->y);
		P_A_U.move(packet->x, packet->y - 1);
		P_A_D.move(packet->x, packet->y + 1);
		P_A_L.move(packet->x - 1, packet->y);
		P_A_R.move(packet->x + 1, packet->y);
		g_left_x = packet->x - SCREEN_WIDTH / 2;
		g_top_y = packet->y - SCREEN_HEIGHT / 2;
		
		players[g_myid].damage = packet->damage;
		//strncpy_s(avatar.nickname, packet->name, NAME_SIZE);
		strncpy_s(players[g_myid].nickname, packet->name, NAME_SIZE);
		avatar.show();
		//P_A_U.show();
	}
	break;

	case SC_ADD_OBJECT:
	{
		//cout << "SC_ADD_OBJECT\n";
		SC_ADD_OBJECT_PACKET* my_packet = reinterpret_cast<SC_ADD_OBJECT_PACKET*>(ptr);
		int id = my_packet->id;
		
		if (id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			P_A_U.move(my_packet->x, my_packet->y - 1);
			P_A_D.move(my_packet->x, my_packet->y + 1);
			P_A_L.move(my_packet->x - 1, my_packet->y);
			P_A_R.move(my_packet->x + 1, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
			players[id].npc_type = my_packet->npc_type;
			players[id].npc_move_type = my_packet->npc_move_type;
			players[id].level = my_packet->level;
			//players[id].id = id;
			//strncpy_s(players[id].nickname, my_packet->name, NAME_SIZE);
			//cout << players[id].nickname << "님이 접속하셨습니다." << endl;
			avatar.show();
		}
		else if (id < MAX_USER) {
			players[id] = OBJECT{ *player, 0, 0, 64, 64 };
			players[id].id = id;
			players[id].damage = my_packet->damage;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			strncpy_s(players[id].nickname, my_packet->name, NAME_SIZE);
			players[id].npc_type = my_packet->npc_type;
			players[id].npc_move_type = my_packet->npc_move_type;
			players[id].level = my_packet->level;
			players[id].show();
			//cout << players[id].nickname << "님이 접속하셨습니다." << endl;
		}
		else {
			//players[id] = OBJECT{ *pieces, 0, 0, 128, 128 };
			if (my_packet->npc_type == NT_PEACE)
				players[id] = OBJECT{ *pieces_m1, 0, 0, 64, 64 };
			else
				players[id] = OBJECT{ *pieces_m2, 0, 0, 64, 64 };
			
			players[id].id = id;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			strncpy_s(players[id].nickname, my_packet->name, NAME_SIZE);
			players[id].damage = my_packet->damage;
			players[id].npc_type = my_packet->npc_type;
			players[id].npc_move_type = my_packet->npc_move_type;
			players[id].level = my_packet->level;
			players[id].show();
			//cout << players[id].nickname << "님이 접속하셨습니다." << endl;

		}
		break;
	}
	case SC_MOVE_OBJECT:
	{
		//cout << "SC_MOVE_OBJECT\n";
		SC_MOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_MOVE_OBJECT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			P_A_U.move(my_packet->x, my_packet->y - 1);
			P_A_D.move(my_packet->x, my_packet->y + 1);
			P_A_L.move(my_packet->x - 1, my_packet->y);
			P_A_R.move(my_packet->x + 1, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH/2;
			g_top_y = my_packet->y - SCREEN_HEIGHT/2;
		}
		else {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		break;
	}

	case SC_REMOVE_OBJECT:
	{
		//cout << "SC_REMOVE_OBJECT\n";
		SC_REMOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_REMOVE_OBJECT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else {	// npc이면
			players.erase(other_id);
			//players[other_id].hide();
		}
		break;
	}
	case SC_CHAT:
	{
		//cout << "SC_CHAT\n";
		SC_CHAT_PACKET* my_packet = reinterpret_cast<SC_CHAT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.set_chat(my_packet->mess);
		}
		else {
			players[other_id].set_chat(my_packet->mess);
		}
		break;
	}
	case SC_ATTACK:

	{
		//cout << "SC_ATTACK\n";
		SC_ATTACK_PACKET* my_packet = reinterpret_cast<SC_ATTACK_PACKET*>(ptr);
		//sprintf_s(clients[i]._name, "NPC%d", i);
		//cout << "NPC" << my_packet->attack_id << "가 " << m_name << endl;
		
		//cout << players[my_packet->attack_id].nickname << "가 " << avatar.nickname << "를 공격했습니다." << endl;
		if(is_npc(my_packet->attack_id) && is_pc(my_packet->target_id))
			cout << players[my_packet->attack_id].nickname << "가 " << players[my_packet->target_id].nickname << "를 공격해 " << players[my_packet->attack_id].damage << "데미지를 입었습니다." << endl;
		else if (is_pc(my_packet->attack_id) && is_npc(my_packet->target_id))
			cout << players[my_packet->attack_id].nickname << "가 " << players[my_packet->target_id].nickname << "를 공격해 " << players[my_packet->attack_id].damage << "데미지를 입혔습니다." << endl;

		break;
	}

	case SC_NPC_WAKED:
	{
		//cout << "SC_NPC_WAKED\n";
		SC_NPC_WAKED_PACKET* my_packet = reinterpret_cast<SC_NPC_WAKED_PACKET*>(ptr);
		players[my_packet->id].show();
		break;
	}

	case SC_USER_INGAMEINFO:
	{
		SC_USER_INGAMEINFO_PACKET * my_packet = reinterpret_cast<SC_USER_INGAMEINFO_PACKET*>(ptr);
		players[g_myid].level = my_packet->level;
		players[g_myid].hp = my_packet->hp;
		players[g_myid].exp = my_packet->exp;
		avatar.level = my_packet->level;
		avatar.hp = my_packet->hp;
		avatar.exp = my_packet->exp;

		//cout<< "level: " << players[g_myid].level << " hp: " << players[g_myid].hp << endl;
		break;
	}
	case SC_DIE:
	{
		SC_DIE_PACKET* my_packet = reinterpret_cast<SC_DIE_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else {	// npc이면
			//players.erase(other_id);
			players[other_id].hide();
			players[other_id].set_endtime();

			if (players[other_id].npc_type == NT_AGRO)
				cout << "npc" << other_id << "를 처치해 " << players[other_id].level * players[other_id].level * 2 * 2 << "의 경험치를 얻었습니다.\n";
			else if (players[other_id].npc_move_type == NT_ROAM)
				cout << "npc" << other_id << "를 처치해 " << players[other_id].level * players[other_id].level * 2 * 2 << "의 경험치를 얻었습니다.\n";
			else
				cout << "npc" << other_id << "를 처치해 " << players[other_id].level * players[other_id].level * 2 << "의 경험치를 얻었습니다.\n";
		}
		break;

	}

	default:
		printf("Unknown PACKET type [%d]\n", ptr[2]);
	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (0 != io_byte) {
		if (0 == in_packet_size) { 
			//in_packet_size = ptr[0]; 
			in_packet_size = static_cast<unsigned short>(*ptr);
			//cout << "in_packet_size: " << in_packet_size << endl;
		}
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

void client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = s_socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		exit(-1);
	}
	if (recv_result == sf::Socket::Disconnected) {
		wcout << L"Disconnected\n";
		exit(-1);
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if (0 ==(tile_x /3 + tile_y /3) % 2) {
				blue_tile.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
				blue_tile.a_draw();
			}
			else
			{
				green_tile.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
				green_tile.a_draw();
			}
		}
	avatar.draw();
	P_A_U.attack_draw(); P_A_D.attack_draw(); P_A_L.attack_draw(); P_A_R.attack_draw();
	avatar.check_recover();
	//if (is_npc(id)) check_endtime();
	for (auto& pl : players) {
		pl.second.draw(); 
		pl.second.check_endtime();
	}
	
	//sf::Text text;
	//text.setFont(g_font);
	//char buf[100];
	//sprintf_s(buf, "(%d, %d)", avatar.m_x, avatar.m_y);
	//text.setString(buf);
	//g_window->draw(text);

	{
		sf::Text text;
		text.setFont(g_font);
		char buf[100];
		sprintf_s(buf, "level: %d hp: %d exp: %d", players[g_myid].level, players[g_myid].hp, players[g_myid].exp);
		text.setString(buf);
		//text.setPosition(0, WINDOW_WIDTH - 1000);
		g_window->draw(text);
	}



}



int main()
{
	wcout.imbue(locale("korean"));
	sf::Socket::Status status = s_socket.connect("127.0.0.1", PORT_NUM);
	s_socket.setBlocking(false);

	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		exit(-1);
	}

	client_initialize();
	CS_LOGIN_PACKET p;
	p.size = sizeof(p);
	p.type = CS_LOGIN;

	string player_name{ "P" };
	player_name += to_string(GetCurrentProcessId());
	
	strcpy_s(p.name, player_name.c_str());
	send_packet(&p);
	avatar.set_name(p.name);
	strncpy_s(avatar.nickname, p.name, NAME_SIZE);

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::KeyPressed) {
				int direction = -1;
				switch (event.key.code) {
				case sf::Keyboard::Left:
					direction = 2;
					break;
				case sf::Keyboard::Right:
					direction = 3;
					break;
				case sf::Keyboard::Up:
					direction = 0;
					break;
				case sf::Keyboard::Down:
					direction = 1;
					break;
				case sf::Keyboard::Escape:
					window.close();
					break;
				case sf::Keyboard::A:
					CS_ATTACK_PACKET p;
					p.size = sizeof(CS_ATTACK_PACKET);
					p.type = CS_ATTACK;
					send_packet(&p);

					P_A_U.show(); P_A_D.show(); P_A_L.show(); P_A_R.show();

					break;
				}
				if (-1 != direction) {
					CS_MOVE_PACKET p;
					p.size = sizeof(p);
					p.type = CS_MOVE;
					p.direction = direction;
					send_packet(&p);
				}

			}
		}

		window.clear();
		client_main();
		window.display();
	}
	client_finish();

	return 0;
}