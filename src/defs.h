/*
**++
**
**  MODULE: N2S-DEFS,  IDENT: X.00-05,  REV: 00.05.00
**
**  FACILITY: A yet another Network to Serial gateway
**
**  ENVIRONMENT: Linux
**
**  DESCRIPTION: Data structures and constant definitions is supposed to be used across all the
**	NET2SERIAL's modules.
**
**  AUTHORS: StarLet Squad and Ruslan R. Laishev (AKA: BadAss sysman)
**
**  CREATION DATE:  10-AUG-2025
**
**  MODIFICATION HISTORY:
**
**	25-AUG-2026	RRL	X.00-05 / REV: 00.05.00 - Audit fixes:
**				the structure tags got the _t suffix as the coding standard requires;
**				n2s$_ring_adjdata() does not clear the <fl_full> flag on a zero count;
**				added the validation limits (N2S$K_BAUD_/DATABITS_/STOPBITS_/PORT_MIN/MAX),
**				the flow control constants (N2S$K_FLOW_*) and the I/O timeout range;
**				N2S$_SERIAL got the <owner_sd> field - a serial line is given to a single
**				session at a time (see the User Guide, the "one client per port" rule);
**				the dead MODBUS leftovers (the PDU states, <inter_pdu_ts>) are removed;
**				added the prototypes of the has been added TTY routines.
**
**--
*/


#ifndef	__NET2SERIAL_DEFS__
#define __NET2SERIAL_DEFS__	1

#include	<netinet/in.h>
#include	<arpa/inet.h>
#include	<termios.h>
#include	<pthread.h>

#include	"utility_routines.h"					/* Starlet's Utility/General purpose routines	*/
#include	"starlet.h"						/* FAO: fao_prm_t for the $PUTMSG_FAO parameters */

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



/*
 *  A state of the session and of the serial device
 */
enum {
	N2S$K_STATE_IDLE = 0,						/* Nothing has been initialized yet		*/

	N2S$K_STATE_READY,						/* Is initialized and is ready for an I/O	*/
	N2S$K_STATE_SHUT,						/* Should be closed				*/

	N2S$K_STATE_EOL
};


/*
 *  A flow control discipline of the serial line
 */
enum {
	N2S$K_FLOW_NONE = 0,						/* No flow control at all			*/
	N2S$K_FLOW_XONXOFF,						/* The software one: XON/XOFF (IXON | IXOFF)	*/
	N2S$K_FLOW_RTSCTS,						/* The hardware one: RTS/CTS (CRTSCTS)		*/

	N2S$K_FLOW_EOL
};


#define		N2S$SZ_BUFF		2048
#define		N2S$K_MAX_LISTENERS	128
#define		N2S$K_MAX_SERIALS	128
#define		N2S$K_IDLE_TMO_SEC	1200				/* A global timeout for the IDLE sessions	*/
#define		N2S$K_NET_TMO_MSEC	3000				/* A default timeout of the session poll()	*/
#define		N2S$K_TTY_DEVNAME	64
#define		N2S$K_TTY_DESC		64

/*
 *  The limits for the validation of the has been configured parameters of a serial line
 *  and of a network listener
 */
#define		N2S$K_BAUD_MIN		50				/* The slowest POSIX line rate			*/
#define		N2S$K_BAUD_MAX		4000000				/* B4000000 - the fastest one			*/
#define		N2S$K_DATABITS_MIN	5				/* CS5						*/
#define		N2S$K_DATABITS_MAX	8				/* CS8						*/
#define		N2S$K_STOPBITS_MIN	1
#define		N2S$K_STOPBITS_MAX	2
#define		N2S$K_PORT_MIN		1				/* The TCP port range				*/
#define		N2S$K_PORT_MAX		65535
#define		N2S$K_IOTMO_MIN		1				/* An I/O timeout range, milliseconds		*/
#define		N2S$K_IOTMO_MAX		600000
#define		N2S$K_CONNLM_MIN	1				/* A backlog of the listening socket		*/
#define		N2S$K_CONNLM_MAX	128



/*
 *   DESCRIPTION: A nano-API to work with the ring buffers. The buffer keeps the octets stream
 *	in the order of arrival; the data and the free space are addressed by the contiguous
 *	chunks, so a chunk is passed to read()/write() directly, without an intermediate copy.
 *
 *	<head> - the oldest octet of the data, <tail> - the first free octet. The both pointers
 *	are equal both for the empty and for the full buffer, so the <fl_full> flag tells them apart.
 */
typedef struct	n2s_ring_t {
	int	sz,							/* A capacity of the buffer, octets		*/
		head, tail,						/* See the description above			*/
		fl_full;						/* 1: the buffer has no free space		*/
	char	*data;							/* An address of the buffer itself		*/
} N2S$_RING;

#define	N2S$RING_DECL(r, ring_sz)  char r ## __data[ring_sz]; N2S$_RING r = {.sz = (ring_sz), .data = r ## __data};


/*
 *   DESCRIPTION: Get an address and a length of the contiguous free space of the ring buffer.
 *
 *   INPUTS:
 *	a_ring:		A context of the ring buffer
 *
 *   OUTPUTS:
 *	a_free:		An address of the first free octet
 *
 *   RETURNS:
 *	A length of the contiguous free space, 0 - the buffer is full
 */
static inline int n2s$_ring_getfree (
		N2S$_RING	*a_ring,
			char	**a_free
		)
{
	if ( a_ring->fl_full )
		return 0;						/* No free space */


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


/*
 *   DESCRIPTION: Declare that <a_count> octets of the free space has been filled by a data.
 *
 *   INPUTS:
 *	a_ring:		A context of the ring buffer
 *	a_count:	A count of the has been filled octets
 *
 *   OUTPUTS:
 *	a_ring:		<tail> and <fl_full> are updated
 *
 *   RETURNS:
 *	NONE
 */
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


/*
 *   DESCRIPTION: Get an address and a length of the contiguous data of the ring buffer.
 *
 *   INPUTS:
 *	a_ring:		A context of the ring buffer
 *
 *   OUTPUTS:
 *	a_data:		An address of the oldest octet of the data
 *
 *   RETURNS:
 *	A length of the contiguous data, 0 - the buffer is empty
 */
static inline int n2s$_ring_getdata (
		N2S$_RING	*a_ring,
			char	**a_data
		)
{
	if ( (!a_ring->fl_full) && (a_ring->head == a_ring->tail) )
		return 0;						/* No data in the buffer */


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


/*
 *   DESCRIPTION: Declare that <a_count> octets of the data has been consumed.
 *
 *   INPUTS:
 *	a_ring:		A context of the ring buffer
 *	a_count:	A count of the has been consumed octets
 *
 *   OUTPUTS:
 *	a_ring:		<head> and <fl_full> are updated
 *
 *   RETURNS:
 *	NONE
 */
static inline void n2s$_ring_adjdata (
		N2S$_RING	*a_ring,
			int	a_count
		)
{
	if ( !a_count )							/* Never clear <fl_full> for a nothing !	*/
		return;

	a_ring->head = (a_ring->head + a_count) % a_ring->sz;
	a_ring->fl_full = 0;						/* Buffer got free space !!!			*/
}



enum {
	__SERIAL_RS485 = 0,
};

enum {
	N2S$M_SERIAL_RS485 = (1 << __SERIAL_RS485),			/* RS485 specific initialization		*/

};


/*
 *  A context of the serial line. The <lock> guards the <owner_sd> field only: the line itself
 *  is given to a single session at a time, see n2s$tty_acquire()/n2s$tty_release().
 */
typedef struct	n2s_serial_t {
		int	state;						/* Device state, see N2S$K_STATE_* constants	*/
	pthread_mutex_t	lock;						/* Guards the <owner_sd> field below		*/
		int	owner_sd;					/* A socket of the owning session, -1 - is free	*/

		int	fd;						/* I/O descriptor for serial device		*/

		char	devname[N2S$K_TTY_DEVNAME + 1];			/* "COM1", "/dev/ttyS0"				*/
		char	desc[N2S$K_TTY_DESC + 1];			/* A human readable description of the device	*/

		int	flags;						/* Optional RS485 specific initialization	*/

		int	baud,						/* Bits-per-second rate of serial line		*/
			stopbits,
			databits,
			parity,						/* 'N', 'E' or 'O'				*/
			flow,						/* See the N2S$K_FLOW_* constants		*/
			iotmo;						/* An I/O timeout for the line, milliseconds	*/

	struct termios	savedtios,
			tios;
} N2S$_SERIAL;



typedef struct	n2s_listener_t {
		int	fd,						/* Network socket descriptor			*/
			proto,						/* IPPROTO_TCP					*/
			iotmo,						/* A timeout of the network I/O, milliseconds	*/
			connlm;						/* A backlog of the listening socket		*/
	struct sockaddr_in sk;						/* Socket address structure: TCP, 0.0.0.0:502	*/
		char	target[N2S$K_TTY_DEVNAME + 1];			/* "COM1", "/dev/ttyS0"				*/

	N2S$_SERIAL	*serial;					/* A context to has been defined serial device	*/

} N2S$_LISTENER;



typedef struct	n2s_session_t {
		int	state;						/* Session state, see N2S$K_STATE_* constants	*/

	struct timespec	lastio_ts;					/* Timestamp of last I/O operation on the session */

		int	sd;						/* Network socket descriptor			*/
	struct sockaddr_in sk;						/* Socket address structure			*/
		char	ep[64];						/* "aa.bb.cc.dd:port" ASCII string		*/

		int	iotmo;						/* A timeout of the network I/O, milliseconds	*/

	N2S$_SERIAL	*target;					/* A serial line this session is bound to	*/
} N2S$_SESSION;


int	n2s$net_start_listeners (void);
int	n2s$net_stop_listeners (void);


int	n2s$tty_open (N2S$_SERIAL *a_serial);
int	n2s$tty_close (N2S$_SERIAL *a_serial);
int	n2s$tty_acquire (N2S$_SERIAL *a_serial, int a_sd, int *a_owner_sd);
int	n2s$tty_release (N2S$_SERIAL *a_serial, int a_sd);
int	n2s$tty_flush (const N2S$_SERIAL *a_serial);
int	n2s$tty_tx (const N2S$_SERIAL *a_serial, N2S$_RING *a_buf_dsc);
int	n2s$tty_rx (const N2S$_SERIAL *a_serial, N2S$_RING *a_buf_dsc);

#ifdef __cplusplus
}
#endif

#endif	/*	__NET2SERIAL_DEFS__	*/
