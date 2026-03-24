
/*
**++
**
**  FACILITY: A yet another gateway for MODBUS (TCP 2 RTU)
**
**  ENVIRONMENT: Linux
**
**  DESCRIPTION: Data Srtuctures and constant definitions is supposed to be used across all MODBUS-TCP2RTU's modules
**
**
**  AUTHORS: Ruslan R. (The BadAss Sysman) Laishev
**
**  CREATION DATE:  10-AUG-2025
**
**  MODIFICATION HISTORY:
**
**--
*/


#ifndef	__NET2SERIAL_DEFS__
#define __NET2SERIAL_DEFS__	1

#include	<netinet/in.h>
#include	<arpa/inet.h>
#include	<termios.h>
#include	<pthread.h>

#include	"utility_routines.h"					/* Starlet's Utility/General purpose routines */

#include	<linux/limits.h>

#ifdef __cplusplus
extern "C" {
#endif





#ifndef IPv4_BYTES
#define IPv4_BYTES_FMT "%u.%u.%u.%u"
#define IPv4_BYTES(addr) \
		(uint8_t) ((addr) & 0xFF), \
		(uint8_t) (((addr) >> 8) & 0xFF),\
		(uint8_t) (((addr) >> 16) & 0xFF),\
		(uint8_t) (((addr) >> 24) & 0xFF)
#endif



enum {
	N2S$K_STATE_IDLE = 0,

	N2S$K_STATE_READY,						/* Idle session context has been created */

	N2S$K_STATE_PDU_RECV,						/* Receiving whole MODBUS's PDU */
	N2S$K_STATE_PDU_ENQD,						/* Received MODBUS PDU is enqueued for processing */

	N2S$K_STATE_PDU_XMIT,						/* We have got an answer from RTU leg and sending answer MODBUS PDU */
	N2S$K_STATE_SHUT,						/* Session should be closed */

	N2S$K_STATE_EOL
};


#define		N2S$SZ_BUFF		2048
#define		N2S$K_MAX_LISTENERS	128
#define		N2S$K_MAX_SERIALS	128
#define		N2S$K_IDLE_TMO_SEC	1200				/* Global timeout for idle sessions */
#define		N2S$K_NET_TMO_SEC	300				/* A timeout for reading data from network socket */
#define		N2S$K_TTY_DEVNAME	64

/*
 *   DESCRIPTION: A nano-API to work with ring buffers
 *
 *   INPUTS:
 *   OUTPUTS:
 *   RETURNS:
 */
typedef struct	n2s$_ring {
	int	sz,
		head, tail,
		fl_full;
	char	*data;
} N2S$_RING;

#define	N2S$RING_DECL(r, ring_sz)  char r ## __data[ring_sz]; N2S$_RING r = {.sz = (ring_sz), .data = r ## __data};

/*
 *
 */
static inline int n2s$_ring_getfree (
		N2S$_RING	*a_ring,
			char	**a_free
		)
{
	if ( a_ring->fl_full )
		return 0;			/* No free space */


	*a_free = a_ring->data + a_ring->tail;

	/*
		    Old --->>> New
	 +-----+--------------------+----------------------+
	 | Free|       Data         |  Free                |
	 +-----+--------------------+----------------------+
	       ^                    ^
	      Head                Tail
	*/
	if ( a_ring->tail >= a_ring->head )
		return a_ring->sz - a_ring->tail;


	/*
					 Old --->>> New
	 +-----+--------------------+----------------------+
	 | Data|       Free         |  Data                |
	 +-----+--------------------+----------------------+
	       ^                    ^
	      Tail                Head
	*/

	return a_ring->head - a_ring->tail;
}

static inline void n2s$_ring_adjfree (
		N2S$_RING	*a_ring,
			int	a_count
		)
{
	if ( !a_count )
		return;

	a_ring->tail = (a_ring->tail + a_count) % a_ring->sz;
	a_ring->fl_full = (a_ring->tail == a_ring->head);
}



static inline int n2s$_ring_getdata (
		N2S$_RING	*a_ring,
			char	**a_data
		)
{
	if ( (!a_ring->fl_full) && (a_ring->head == a_ring->tail) )
		return 0;							/* No data in the buffer */


	*a_data = a_ring->data + a_ring->head;

	/*
		    Old --->>> New
	 +-----+--------------------+----------------------+
	 | Free|       Data         |  Free                |
	 +-----+--------------------+----------------------+
	       ^                    ^
	      Head                Tail
	*/
	if ( a_ring->head < a_ring->tail )
		return	a_ring->tail - a_ring->head;


	/*
					 Old --->>> New
	 +-----+--------------------+----------------------+
	 | Data|       Free         |  Data                |
	 +-----+--------------------+----------------------+
	       ^                    ^
	      Tail                Head
	*/
	return	a_ring->sz - a_ring->head;

}

static inline void n2s$_ring_adjdata (
		N2S$_RING	*a_ring,
			int	a_count
		)
{
	a_ring->head = (a_ring->head + a_count) % a_ring->sz;
	a_ring->fl_full = 0;
}



enum {
	__SERIAL_RS485 = 0,
};

enum {
	N2S$M_SERIAL_RS485 = (1 << __SERIAL_RS485),			/* RS485 specific initialization */

};

typedef struct	n2s$_serial {
		int	state;						/* Session state, see N2S$K_STATE_* constants */
	pthread_mutex_t	lock;						/* Coordinate an access between multiple TCP clients */

		int	fd;						/* I/O descriptor for serial device */
	struct timespec							/* See <pdutmo> */
			inter_pdu_ts;

		char devname[N2S$K_TTY_DEVNAME + 1];			/* "COM1", "/dev/ttyS0" */


		int	flags;						/* Optional RS485 specific initialization */

		int	baud,						/* Bits-per-second rate of serial line */
			stopbits,
			databits,
			parity,
			flow;

	struct termios	savedtios,
			tios;
} N2S$_SERIAL;



typedef struct	n2s$_listener {
		int	fd,						/* Network socket descriptor */
			proto,						/* IPPROTO_TCP or IPPROTO_UDP */
			iotmo,
			connlm;						/* Connection limit on this listener */
	struct sockaddr_in sk;						/* Socket address structure: TCP, 0.0.0.0:502 */
		char target[N2S$K_TTY_DEVNAME + 1];			/* "COM1", "/dev/ttyS0" */

	N2S$_SERIAL	*serial;					/* A context to has been defined serial device */

} N2S$_LISTENER;





typedef struct	n2s$__session {
		int	state;						/* Session state, see N2S$K_STATE_* constants */

	struct timespec	lastio_ts;					/* Timestamp of last I/O operation on the session */

		int	sd;						/* Network socket descriptor */
	struct sockaddr_in sk;						/* Socket address structure */

		void	*netbuf_dsc,					/* A descriptor of buffer for network I/O */
			*ttybuf_dsc;					/* A descriptor of buffer for serial I/O */

	N2S$_SERIAL	*target;
} N2S$_SESSION;


int	n2s$net_start_listeners (void);
int	n2s$net_stop_listeners (void);


int	n2s$tty_open(N2S$_SERIAL *);
int	n2s$tty_close(N2S$_SERIAL *);
int	n2s$tty_tx (const N2S$_SERIAL *, N2S$_RING *);
int	n2s$tty_rx (const N2S$_SERIAL *, N2S$_RING *);

#ifdef __cplusplus
}
#endif

#endif	/*	__NET2SERIAL_DEFS__	*/
