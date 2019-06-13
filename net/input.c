#include "ns.h"

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server (using ipc_send with NSREQ_INPUT as value)
	//	do the above things in a loop
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	char buf[2048];
	int r, read;

	while (1) {
		if ((read = sys_net_recv(buf, 2048)) < 0) {
			sys_yield();
			continue;
		}
		if ((r = sys_page_alloc(0, (void *)&nsipcbuf, PTE_P | PTE_U | PTE_W)) < 0)
			panic("in input, sys_page_alloc: %e", r);
		nsipcbuf.pkt.jp_len = read;
		memmove(nsipcbuf.pkt.jp_data, buf, read);
		ipc_send(ns_envid, NSREQ_INPUT, (void *)&nsipcbuf, PTE_P | PTE_U | PTE_W);
	}
}
