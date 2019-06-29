#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/error.h>

static struct E1000 *base;

struct tx_desc *tx_descs;
#define N_TXDESC (PGSIZE / sizeof(struct tx_desc))

#define TX_BUFSIZE 1518
static uint8_t tx_buf[TX_BUFSIZE * N_TXDESC];

int
e1000_tx_init()
{
	struct PageInfo *pp;
	int i;
	// Allocate one page for descriptors
	pp = page_alloc(ALLOC_ZERO);

	// Initialize all descriptors
	tx_descs = page2kva(pp);

	for (i = 0; i < N_TXDESC; i++) {
		tx_descs[i].addr = PADDR(tx_buf + TX_BUFSIZE * i);
		tx_descs[i].length = 0;
		tx_descs[i].cmd |= E1000_TX_CMD_EOP;
		tx_descs[i].cmd |= E1000_TX_CMD_RS;
		tx_descs[i].status |= E1000_TX_STATUS_DD;
	}
	// Set hardward registers
	// Look kern/e1000.h to find useful definations
	base->TDBAL = page2pa(pp);
	base->TDBAH = 0;
	base->TDLEN = N_TXDESC * sizeof(struct tx_desc);
	base->TDH = 0;
	base->TDT = 0;
	base->TCTL |= E1000_TCTL_EN;
	base->TCTL |= E1000_TCTL_PSP;
	base->TCTL |= E1000_TCTL_CT_ETHER;
	base->TCTL |= E1000_TCTL_COLD_FULL_DUPLEX;
	base->TIPG = E1000_TIPG_DEFAULT;

	return 0;
}

struct rx_desc *rx_descs;
#define N_RXDESC (PGSIZE / sizeof(struct rx_desc))

#define RX_BUFSIZE 2048
static uint8_t rx_buf[RX_BUFSIZE * N_RXDESC];

int
e1000_rx_init()
{
	struct PageInfo *pp;
	int i;
	uint8_t mac[6];
	uint32_t mac_low;
	uint32_t mac_high;
	// Allocate one page for descriptors
	pp = page_alloc(ALLOC_ZERO);
	// Initialize all descriptors
	// You should allocate some pages as receive buffer
	rx_descs = page2kva(pp);
	for (i = 0; i < N_RXDESC; i++) {
		rx_descs[i].addr = PADDR(rx_buf + RX_BUFSIZE * i);
		rx_descs[i].length = 0;
	}

	// Set hardward registers
	// Look kern/e1000.h to find useful definations
	e1000_getmac(mac);
	mac_low = mac[0] | mac[1] << 8 | mac[2] << 16 | mac[3] << 24;
	mac_high = mac[4] | mac[5] << 8;

	base->RAL = mac_low;
	base->RAH = mac_high;
	base->RDBAL = page2pa(pp);
	base->RDBAH = 0;
	base->RDLEN = N_RXDESC * sizeof(struct rx_desc);
	base->RDH = 0;
	base->RDT = N_RXDESC - 1;
	base->RCTL |= E1000_RCTL_EN;
	base->RCTL |= E1000_RCTL_BSIZE_2048;
	base->RCTL |= E1000_RCTL_SECRC;
	
	return 0;
}

int
pci_e1000_attach(struct pci_func *pcif)
{
	// Enable PCI function
	// Map MMIO region and save the address in 'base;
	pci_func_enable(pcif);

	base = (struct E1000*)mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);

	e1000_tx_init();
	e1000_rx_init();
	return 0;
}

int
e1000_tx(const void *buf, uint32_t len)
{
	// Send 'len' bytes in 'buf' to ethernet
	// Hint: buf is a kernel virtual address
	size_t nt;
	size_t i;
	void *src = (void *)buf;

	nt = ROUNDUP(len, TX_BUFSIZE) / TX_BUFSIZE;
	for (i = 0; i < nt; i++) {
		if (!(tx_descs[base->TDT].status & E1000_TX_STATUS_DD))
			return -1;
		tx_descs[base->TDT].status &= ~E1000_TX_STATUS_DD;
		tx_descs[base->TDT].length = i == nt - 1? len % TX_BUFSIZE : TX_BUFSIZE;
		memmove(tx_buf + base->TDT * TX_BUFSIZE, 
		src + i * TX_BUFSIZE, i == nt - 1? len % TX_BUFSIZE : TX_BUFSIZE);
		base->TDT = (base->TDT + 1) % N_TXDESC;
	}

	return 0;
}

int
e1000_rx(void *buf, uint32_t len)
{
	// Copy one received buffer to buf
	// You could return -E_AGAIN if there is no packet
	// Check whether the buf is large enough to hold
	// the packet
	// Do not forget to reset the decscriptor and
	// give it back to hardware by modifying RDT
	uint32_t to_read = (base->RDT + 1) % N_RXDESC;

	if (!(rx_descs[to_read].status & E1000_RX_STATUS_DD))
		return -E_AGAIN;

	memmove(buf, rx_buf + to_read * RX_BUFSIZE, MIN(rx_descs[to_read].length, len));
	rx_descs[to_read].status &= ~E1000_RX_STATUS_DD;
	base->RDT = to_read;

	return MIN(rx_descs[to_read].length, len);
}

int
e1000_getmac(void *mac_store)
{
	int i;
	uint8_t mac[6];
	uint16_t data;

	for (i = 0; i < 3; i++) {
		base->EERD = 0;
		base->EERD |= E1000_EERD_SHIFT_ADDR(i);
		base->EERD |= E1000_EERD_START;
		while(!(base->EERD & E1000_EERD_DONE));
		data = E1000_EERD_GET_DATA(base->EERD);
		mac[2 * i] = data & 0xff;
		mac[2 * i + 1] = (data >> 8) & 0xff;
	}
	memmove(mac_store, mac, 6);

	return 0;
}