#pragma once
constexpr int PORT_NUM = 4000;
constexpr int NAME_SIZE = 20;
constexpr int CHAT_SIZE = 100;
constexpr int BUF_SIZE = 200;   //++

constexpr int MAX_USER = 10000;
constexpr int MAX_NPC = 100000;
//constexpr int MAX_NPC = 10000;
constexpr int MAX_CLOUD = 100000;

constexpr int W_WIDTH = 2000;
constexpr int W_HEIGHT = 2000;

constexpr int SECTOR_SIZE = 10;      //++
constexpr int SECTOR_COUNT_X = W_WIDTH / SECTOR_SIZE;   //++
constexpr int SECTOR_COUNT_Y = W_HEIGHT / SECTOR_SIZE;   //++

// Packet ID
constexpr char CS_LOGIN = 0;
constexpr char CS_MOVE = 1;
constexpr char CS_CHAT = 2;
constexpr char CS_ATTACK = 3;         // 4 방향 공격
constexpr char CS_TELEPORT = 4;         // RANDOM한 위치로 Teleport, Stress Test할 때 Hot Spot현상을 피하기 위해 구현
constexpr char CS_LOGOUT = 5;         // 클라이언트에서 정상적으로 접속을 종료하는 패킷
constexpr char CS_NPC_WAKED = 6;         // 클라이언트에서 정상적으로 접속을 종료하는 패킷
constexpr char CS_RECOVER = 7;         // 클라이언트에서 정상적으로 접속을 종료하는 패킷
constexpr char CS_ATTACK_A = 8;         // 클라이언트에서 정상적으로 접속을 종료하는 패킷
constexpr char CS_ATTACK_D = 9;         // 클라이언트에서 정상적으로 접속을 종료하는 패킷

constexpr char SC_LOGIN_INFO = 2;
constexpr char SC_LOGIN_FAIL = 3;
constexpr char SC_ADD_OBJECT = 4;
constexpr char SC_REMOVE_OBJECT = 5;
constexpr char SC_MOVE_OBJECT = 6;
constexpr char SC_CHAT = 7;
constexpr char SC_STAT_CHANGE = 8;
constexpr char SC_LOGIN_OK = 9;   //++
constexpr char SC_ATTACK = 10;   //++
constexpr char SC_NPC_WAKED = 11;   //++
constexpr char SC_USER_INGAMEINFO = 12;   //++
constexpr char SC_DIE = 13;   //++
constexpr char SC_CLOUD = 14;   //++
constexpr char SC_ITEM = 15;   //++

constexpr int VIEW_RANGE = 5;
constexpr int NPC_VIEW_RANGE = 3;

enum N_TYPE { NT_PEACE, NT_AGRO, NT_PLAYER, NT_FIX, NT_ROAM };

#pragma pack (push, 1)
struct CS_LOGIN_PACKET {
	unsigned short size;
	char   type;
	char   name[NAME_SIZE];
};

struct CS_MOVE_PACKET {
	unsigned short size;
	char   type;
	char   direction;  // 0 : UP, 1 : DOWN, 2 : LEFT, 3 : RIGHT
	unsigned   move_time;
};

struct CS_CHAT_PACKET {
	unsigned short size;         // 크기가 가변이다, mess가 작으면 size도 줄이자.
	char   type;
	char   mess[CHAT_SIZE];
};

struct CS_TELEPORT_PACKET {
	unsigned short size;
	char   type;
};

struct CS_LOGOUT_PACKET {
	unsigned short size;
	char   type;
};

struct CS_NPC_WAKED_PACKET {
	unsigned short size;
	char   type;
	int      id;
};

struct CS_ATTACK_PACKET {
	unsigned short size;
	char   type;
};

struct CS_RECOVER_PACKET {
	unsigned short size;
	char   type;
};

struct CS_ATTACK_A_PACKET {
	unsigned short size;
	char   type;
};

struct CS_ATTACK_D_PACKET {
	unsigned short size;
	char   type;
};

struct SC_LOGIN_INFO_PACKET {
	unsigned short size;
	char   type;
	int      id;
	int      hp;
	int      max_hp;
	int      exp;
	int      level;
	int		damage;
	short   x, y;
	char   name[NAME_SIZE];
};

struct SC_ADD_OBJECT_PACKET {
	unsigned short size;
	char   type;
	int      id;
	short   x, y;
	char   name[NAME_SIZE];
	int		 damage;
	int level;
	N_TYPE   npc_type;
	N_TYPE   npc_move_type;
};

struct SC_REMOVE_OBJECT_PACKET {
	unsigned short size;
	char   type;
	int      id;
};

struct SC_MOVE_OBJECT_PACKET {
	unsigned short size;
	char   type;
	int      id;
	short   x, y;
	unsigned int move_time;
};

struct SC_CHAT_PACKET {
	unsigned short size;
	char   type;
	int      id;
	char   mess[CHAT_SIZE];
};

struct SC_LOGIN_FAIL_PACKET {
	unsigned short size;
	char   type;

};

struct SC_STAT_CHANGEL_PACKET {
	unsigned short size;
	char   type;
	int      hp;
	int      max_hp;
	int      exp;
	int      level;

};

struct SC_ATTACK_PACKET {
	unsigned short size;
	char   type;
	int      attack_id;
	int      target_id;
};

struct SC_NPC_WAKED_PACKET {
	unsigned short size;
	char   type;
	int      id;
};

struct SC_USER_INGAMEINFO_PACKET {
	unsigned short size;
	char   type;
	int    level;
	int    hp;
	int    exp;
};

struct SC_DIE_PACKET {
	unsigned short size;
	char   type;
	int      id;
};

struct SC_CLOUD_PACKET {
	unsigned short size;
	char   type;
	int id;
	short   x, y;
	bool  in_see;
};

struct SC_ITEM_PACKET {
	unsigned short size;
	char   type;
	int id;
};

struct POSITION {
	int x, y;
};

#pragma pack (pop)