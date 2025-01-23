#pragma once
#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include <windows.h>
#include "protocol.h"

void worker_thread(HANDLE h_iocp);
bool in_npc_see(int from, int to);
void do_timer();
bool movePossible(POSITION& player);
bool can_see(int from, int to);

#endif // WORKER_THREAD_H
