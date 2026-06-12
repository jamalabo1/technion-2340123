//
// Created by jamal on 12/06/2026.
//
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <iostream>
#include <cassert>
#include <bitset> // Required header

using namespace std;

long set_ban(int ban_getpid, int ban_pipe, int ban_kill, int ban_sig) {
    long r = syscall(334, ban_getpid, ban_pipe, ban_kill, ban_sig);
    return r;
}

long get_ban(char ban) {
    long r = syscall(335, ban);
    return r;
}

long check_ban(pid_t pid, char ban) {
	long r = syscall(336, pid, ban);
    return r;
}

long flip_ban_branch(int height, char ban) {
	long r = syscall(337, height, ban);
    return r;
}



// int main() {

//     cout << "banning" << endl;
//     set_ban(0,0,0,1);

//     cout << "geting ban" << endl;
//     // is ignored for now
//     long flag = get_ban(1);

//     cout <<  std::bitset<32>(flag) << endl;

//     return 0;
// }

int main() {
	long x = get_ban('g');
	cout << "getpid() should not be banned initially (0) -> " << x << endl;
	assert(x == 0);

	x = set_ban(1,1,0,0);
	cout << "set_ban(1100) returned: " << x;
	if(x == -1) cout << " (did you use sudo?)";
	cout << endl;

	assert(x == 0);
	x = get_ban('g');
	cout << "getpid() should be banned (1) -> " << x << endl;
	assert(x == 1);

	x = flip_ban_branch(1, 'k');
	cout << "set_flip_branch returned: " << x << endl;
	assert(x >= 0);

	x = check_ban(pid_t(getppid()), 'k');
	cout << "Our parent should be banned from using kill() (1) -> " << x << endl;
	assert(x == 1);

	x = getpid();
	cout << "getpid() returned: " << x << endl;
	assert(x == -1);

	cout << "===== SUCCESS =====" << endl;
	return 0;
}

