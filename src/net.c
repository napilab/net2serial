#define	__MODULE__	"N2S-NET"
#define	__IDENT__	"X.00-05"
#define	__REV__		"00.05.00"

/*
**++
**
**  FACILITY: A yet another Network to Serial gateway
**
**  ENVIRONMENT: Linux
**
**  DESCRIPTION: A set of routines related to the network I/O: the connections dispatcher and
**	the per-session worker which pumps the octets stream in the both directions.
**
**  AUTHORS: StarLet Squad and Ruslan R. Laishev (AKA: BadAss sysman)
**
**  CREATION DATE:  26-SEP-2025
**
**  MODIFICATION HISTORY:
**
**	25-AUG-2026	RRL	X.00-05 / REV: 00.05.00 - Audit fixes:
**				__MODULE__ is "N2S-NET" now (was "N2S$-NET" - the $ has no business in
**				a facility name of the log);
**				s_net_session(): the whole poll() loop is reworked - the requested events
**				are computed from the state of the ring buffers at every round (were:
**				mutated incrementally and were lost at any timeout), a poll() timeout does
**				not process the stale .revents anymore, POLLERR/POLLHUP/POLLNVAL are
**				handled (an unplugged USB adapter gave a 100% CPU busy loop), <lastio_ts>
**				is updated on every successful I/O (an ACTIVE session was killed by the
**				idle timeout), EINTR is tolerated, pthread_detach() - the session threads
**				were never joined (a resource leak);
**				s_net_rx()/s_net_tx(): fixed the $LOG argument lists, EAGAIN/EINTR are a
**				normal flow now, a zero return of send() is not a "peer close" anymore;
**				s_net_listener(): fixed a detection of the accept() failure (the condition
**				was always false), calloc() check, the sockaddr length is reset before
**				every accept(), the serial line is acquired BEFORE the session thread is
**				started - a second client of a busy line is rejected with a diagnostic;
**				n2s$net_start_listeners(): the pollfd array is initialized properly, the
**				target device is opened BEFORE the socket is published, the listening
**				socket is not leaked on the error paths, TCP keepalive is switched on;
**				n2s$net_stop_listeners(): the serial devices are closed over the table of
**				serials (were: per listener, so a shared device was closed twice).
**
**--
*/

#include	<time.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>
#include	<errno.h>
#include	<unistd.h>
#include	<poll.h>
#include	<netinet/tcp.h>


#define		__FAC__	"NET2SER"
#define		__TFAC__ __FAC__ ": "					/* Special prefix for $TRACE			*/

#include	"utility_routines.h"
#include	"defs.h"
#include	"msgs.h"



extern int	g_exit_flag, g_trace;


extern N2S$_LISTENER	g_listeners[];
extern int		g_listeners_nr;

extern N2S$_SERIAL	g_serials[];					/* Is used by n2s$net_stop_listeners()		*/
extern int		g_serials_nr;

static struct pollfd	s_pfd_lsnr[N2S$K_MAX_LISTENERS];		/* Initialized in n2s$net_start_listeners()	*/

static const	int	s_one = 1;



/*
 *   DESCRIPTION: Read a next portion of the data from the session's TCP socket into the ring
 *	buffer. A single recv() is performed into the contiguous free chunk of the ring.
 *
 *   INPUTS:
 *	a_session:	A context of the network session
 *
 *   OUTPUTS:
 *	a_buf_dsc:	The ring buffer, is advanced by the count of the has been read octets
 *
 *   RETURNS:
 *	condition code; STS$K_WARN - the peer has closed the connection
 */
static int	s_net_rx (
		N2S$_SESSION	*a_session,
		N2S$_RING	*a_buf_dsc
		)
{
int	l_rc, l_len;
char	*l_data;


	if ( !(l_len = n2s$_ring_getfree(a_buf_dsc, &l_data)) )			/* Get free space for incoming data	*/
		return	STS$K_SUCCESS;						/* No space ---- just return		*/


									/* Read a data from socket */
	if ( !(l_rc = recv(a_session->sd, l_data, l_len, MSG_NOSIGNAL)) )
		return	$LOG(STS$K_WARN, "[#%d:<%s>] --- peer close connection", a_session->sd, a_session->ep);

	if ( 0 > l_rc )								/* Check return status			*/
		{
		if ( (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR) )
			return	STS$K_SUCCESS;					/* Not an error - come back later	*/

		return	$PUTMSG_FAO(N2S$__NETIOERR, a_session->sd, a_session->ep, "recv()", errno);
		}


	n2s$_ring_adjfree(a_buf_dsc, l_rc);					/* Adjust ring's buffer internals	*/

	$IFTRACE(g_trace, "[#%d:<%s>] Read %d octets", a_session->sd, a_session->ep, l_rc);

	return	STS$K_SUCCESS;
}


/*
 *   DESCRIPTION: Send a next contiguous chunk of the data from the ring buffer to the session's
 *	TCP socket. A partial send is a normal case, so the ring is advanced by the really sent
 *	count and the rest goes at the next round.
 *
 *   INPUTS:
 *	a_session:	A context of the network session
 *
 *   OUTPUTS:
 *	a_buf_dsc:	The ring buffer, is advanced by the count of the has been sent octets
 *
 *   RETURNS:
 *	condition code
 */
static int	s_net_tx (
		N2S$_SESSION	*a_session,
		N2S$_RING	*a_buf_dsc
		)
{
int	l_rc, l_len;
char	*l_data;


	if ( !(l_len = n2s$_ring_getdata (a_buf_dsc, &l_data)) )
		return	STS$K_SUCCESS;

	if ( 0 > (l_rc = send(a_session->sd, l_data, l_len, MSG_NOSIGNAL)) )	/* Check return status			*/
		{
		if ( (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR) )
			return	STS$K_SUCCESS;					/* The socket buffer is full - come later */

		return	$PUTMSG_FAO(N2S$__NETIOERR, a_session->sd, a_session->ep, "send()", errno);
		}


	n2s$_ring_adjdata(a_buf_dsc, l_rc);					/* Adjust ring's buffer internals	*/

	$IFTRACE(g_trace, "[#%d:<%s>] Sent %d octets (from %d)", a_session->sd, a_session->ep, l_rc, l_len);

	return	STS$K_SUCCESS;
}




/*
 *   DESCRIPTION: A worker to serve a single TCP-client:
 *	- read the data from the socket, send it to the serial line
 *	- read the octets from the serial line, send them to the socket
 *	- perform the error handling and terminate the session if need
 *
 *	The requested poll() events are computed from the state of the two ring buffers at every
 *	round: we want to read from a side only when there is a free space for its data, and we
 *	want to write to a side only when there is a data to be written. This keeps the flow
 *	control honest and gives a natural back pressure to a fast side of the pair.
 *
 *   INPUTS:
 *	a_arg:		A session context, has been allocated by the dispatcher
 *
 *   OUTPUTS:
 *	NONE
 *
 *   RETURNS:
 *	NULL - is required by the pthread API only
 */
static void *	s_net_session( void *a_arg)
{
int	l_rc, l_tmo;
enum {PFD$K_NET = 0, PFD$K_TTY, PFD$K_MAX};
struct pollfd l_pfd[PFD$K_MAX] = {0};
N2S$_SESSION	*l_session;
struct timespec	l_now;
N2S$_SERIAL	*l_serial;
char	*l_data;
N2S$RING_DECL	(l_netbuf_dsc, N2S$SZ_BUFF);
N2S$RING_DECL	(l_ttybuf_dsc, N2S$SZ_BUFF);

	pthread_detach(pthread_self());					/* Nobody joins us: release the TCB at exit	*/

	l_session = a_arg;
	assert ( l_session );
	l_serial = l_session->target;
	assert ( l_serial );


	l_session->state = N2S$K_STATE_READY;

	l_tmo = (N2S$K_IOTMO_MIN <= l_session->iotmo) ? l_session->iotmo : N2S$K_NET_TMO_MSEC;

	/*
	 * Drop a garbage which the device could have left from a previous dialogue: a new client
	 * must not get a tail of somebody else's session.
	 */
	n2s$tty_flush (l_serial);

	clock_gettime(CLOCK_MONOTONIC, &l_session->lastio_ts);


	while ( !g_exit_flag )
		{
		/*
		 * Check the global timeout of an idle session. The timestamp is updated at every
		 * successful I/O below, so an ACTIVE session lives as long as it is needed.
		 */
		clock_gettime(CLOCK_MONOTONIC, &l_now);

		if ( (l_session->lastio_ts.tv_sec + N2S$K_IDLE_TMO_SEC) < l_now.tv_sec)
			{
			$PUTMSG_FAO(N2S$__SESSTMO, l_session->sd, N2S$K_IDLE_TMO_SEC);
			break;
			}

		/*
		 * Compute the requested events from the state of the ring buffers.
		 */
		l_pfd[PFD$K_NET] = (struct pollfd) {.fd = l_session->sd, .events = 0};
		l_pfd[PFD$K_TTY] = (struct pollfd) {.fd = l_serial->fd,  .events = 0};

		if ( n2s$_ring_getfree(&l_netbuf_dsc, &l_data) )		/* Is there a room for the network data ? */
			l_pfd[PFD$K_NET].events |= POLLIN;

		if ( n2s$_ring_getdata(&l_ttybuf_dsc, &l_data) )		/* Is there a data to be sent to network ? */
			l_pfd[PFD$K_NET].events |= POLLOUT;

		if ( n2s$_ring_getfree(&l_ttybuf_dsc, &l_data) )		/* Is there a room for the serial data ?  */
			l_pfd[PFD$K_TTY].events |= POLLIN;

		if ( n2s$_ring_getdata(&l_netbuf_dsc, &l_data) )		/* Is there a data to be sent to serial ? */
			l_pfd[PFD$K_TTY].events |= POLLOUT;


		if ( 0 > (l_rc = poll(l_pfd, PFD$K_MAX, l_tmo)) )
			{
			if ( errno == EINTR )
				continue;

			$PUTMSG_FAO(N2S$__NETIOERR, l_session->sd, l_session->ep, "poll()", errno);
			break;
			}

		if ( !l_rc )
			continue;					/* A timeout: .revents are NOT updated !	*/


		/*
		 * A failure of any of the two legs terminates the session: an unplugged USB adapter
		 * or a has been reset connection would otherwise give a busy loop on POLLERR/POLLHUP.
		 */
		if ( l_pfd[PFD$K_TTY].revents & (POLLERR | POLLHUP | POLLNVAL) )
			{
			$PUTMSG_FAO(N2S$__LINKDOWN, l_serial->fd, l_serial->devname, l_pfd[PFD$K_TTY].revents);
			break;
			}

		if ( l_pfd[PFD$K_NET].revents & (POLLERR | POLLHUP | POLLNVAL) )
			{
			$LOG(STS$K_WARN, "[#%d:<%s>] --- connection is broken (revents: %#x)", l_session->sd,
				l_session->ep, l_pfd[PFD$K_NET].revents);
			break;
			}


		if ( l_pfd[PFD$K_NET].revents & POLLIN )			/* Incoming data from network ?		*/
			{
			if ( !(1 & (l_rc = s_net_rx (l_session, &l_netbuf_dsc))) )
				break;

			clock_gettime(CLOCK_MONOTONIC, &l_session->lastio_ts);
			}

		if ( l_pfd[PFD$K_NET].revents & POLLOUT )			/* Socket is ready to accept data ?	*/
			{
			if ( !(1 & (l_rc = s_net_tx (l_session, &l_ttybuf_dsc))) )
				break;

			clock_gettime(CLOCK_MONOTONIC, &l_session->lastio_ts);
			}

		if ( l_pfd[PFD$K_TTY].revents & POLLIN )			/* Incoming octets from the serial ?	*/
			{
			if ( !(1 & (l_rc = n2s$tty_rx (l_serial, &l_ttybuf_dsc))) )
				{
				$PUTMSG_FAO(N2S$__TTYIOERR, l_serial->fd, l_serial->devname, "read()", errno);
				break;
				}

			clock_gettime(CLOCK_MONOTONIC, &l_session->lastio_ts);
			}

		if ( l_pfd[PFD$K_TTY].revents & POLLOUT )			/* Serial is ready to accept data ?	*/
			{
			if ( !(1 & (l_rc = n2s$tty_tx (l_serial, &l_netbuf_dsc))) )
				{
				$PUTMSG_FAO(N2S$__TTYIOERR, l_serial->fd, l_serial->devname, "write()", errno);
				break;
				}

			clock_gettime(CLOCK_MONOTONIC, &l_session->lastio_ts);
			}
		}


	/*
	 * Push out whatever is still waiting for the serial line: the operator's last line must
	 * not be lost just because the client has dropped the connection.
	 */
	n2s$tty_tx (l_serial, &l_netbuf_dsc);

	n2s$tty_release (l_serial, l_session->sd);			/* Give the line to the next client	*/

	$PUTMSG_FAO(N2S$__NETDISCN, l_session->sd, l_session->ep);

	close(l_session->sd);
	free( (void *) l_session);

	return	NULL;
}




/*
 *   DESCRIPTION: A thread routine of the connections dispatcher. Waits (by polling the whole
 *	table of the listening sockets) for the incoming TCP connection requests, accepts them,
 *	takes the target serial line for the new session and starts a detached per-session thread.
 *	A request to a line which is busy with another session is rejected with a diagnostic.
 *
 *   INPUTS:
 *	a_arg:		Unused, is required by the pthread API only
 *
 *   OUTPUTS:
 *	NONE
 *
 *   IMPLICITE INPUTS:
 *	s_pfd_lsnr, g_listeners, g_listeners_nr, g_exit_flag
 *
 *   RETURNS:
 *	NULL - is required by the pthread API only
 */
static void *	s_net_listener( void *a_arg)
{
int	l_rc, l_sd, l_owner_sd;
struct sockaddr_in	l_sk = {0};
socklen_t l_slen;
N2S$_SESSION	*l_session;
N2S$_LISTENER	*l_listener;
pthread_t	l_tid;
char	l_ep[64];

	while ( !g_exit_flag )
		{
		/*
		 * Wait for any new TCP-connection request on all ports ...
		 */
		if ( 0 > (l_rc = poll(s_pfd_lsnr, g_listeners_nr, 5000)) )
			{
			if ( errno == EINTR )
				continue;

			$LOG(STS$K_ERROR, "poll()->%d, errno: %d --- listener is aborted", l_rc, errno);
			break;						/* A persistent error would give a busy loop	*/
			}

		if ( !l_rc )
			continue;					/* Timeout - .revents are not updated		*/


		/*
		 * Run over all listeners and check for POLLIN (TCP-connection request)
		 */
		for (int i = 0; i < g_listeners_nr; i++ )
			{
			if ( !(s_pfd_lsnr[i].revents & POLLIN) )		/* Is there any connection request ?		*/
				continue;

			l_listener = &g_listeners[i];

			l_slen = sizeof(l_sk);				/* accept() updates it - reset before every call */

			if ( 0 > (l_sd = accept(s_pfd_lsnr[i].fd, (struct sockaddr *) &l_sk, &l_slen)) )
				{
				if ( (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR) )
					continue;

				$LOG(STS$K_ERROR, "accept(%d)->%d, errno: %d", s_pfd_lsnr[i].fd, l_sd, errno);
				continue;
				}

			snprintf(l_ep, sizeof(l_ep), "" IPv4_BYTES_FMT ":%d",
				IPv4_BYTES(l_sk.sin_addr.s_addr), ntohs(l_sk.sin_port) );

			/*
			 * An octets stream has no framing, so a serial line is given to a single
			 * session at a time: a second client would interleave its octets with the
			 * octets of the first one in the both directions.
			 */
			if ( !(1 & n2s$tty_acquire (l_listener->serial, l_sd, &l_owner_sd)) )
				{
				$PUTMSG_FAO(N2S$__DEVBUSY, l_sd, l_ep, l_listener->serial->devname, l_owner_sd);
				close(l_sd);
				continue;
				}

			$PUTMSG_FAO(N2S$__NETCONN, l_sd, &l_sk.sin_addr, ntohs(l_sk.sin_port), s_pfd_lsnr[i].fd);

			if ( (l_rc = setsockopt(l_sd, IPPROTO_TCP, TCP_NODELAY, (char *) &s_one, sizeof(s_one))) )
				$LOG(STS$K_WARN, "setsockopt(%d, TCP_NODELAY)->%d, errno: %d", l_sd, l_rc, errno);

			/*
			 * The keepalive lets us to detect a silently died client (a power loss, a
			 * gone Wi-Fi): otherwise it would hold the serial line until the idle timeout.
			 */
			if ( (l_rc = setsockopt(l_sd, SOL_SOCKET, SO_KEEPALIVE, (char *) &s_one, sizeof(s_one))) )
				$LOG(STS$K_WARN, "setsockopt(%d, SO_KEEPALIVE)->%d, errno: %d", l_sd, l_rc, errno);


			if ( !(l_session = calloc(1, sizeof(N2S$_SESSION))) )
				{
				$LOG(STS$K_ERROR, "calloc(%d octets)->NULL, errno: %d", (int) sizeof(N2S$_SESSION), errno);
				n2s$tty_release (l_listener->serial, l_sd);
				close(l_sd);
				continue;
				}

			l_session->sd = l_sd;
			l_session->sk = l_sk;
			l_session->target = l_listener->serial;
			l_session->iotmo = l_listener->iotmo;
			memcpy(l_session->ep, l_ep, sizeof(l_session->ep));

									/* Start dedicated thread for session		*/
			if ( (l_rc = pthread_create(&l_tid, NULL, s_net_session, l_session)) )
				{
				$LOG(STS$K_ERROR, "Cannot start network session thread, pthread_create()->%d, errno: %d",
					l_rc, errno);

				n2s$tty_release (l_listener->serial, l_sd);
				close(l_sd);
				free(l_session);
				}
			}
		}

	return	NULL;
}



/*
 *   DESCRIPTION: Stop the network subsystem: close all the has been opened listening sockets,
 *	then close all the has been opened serial devices. Is called at the exit path of the
 *	main routine.
 *
 *   INPUTS:
 *	NONE
 *
 *   OUTPUTS:
 *	NONE
 *
 *   IMPLICITE OUTPUTS:
 *	s_pfd_lsnr, g_serials
 *
 *   RETURNS:
 *	condition code
 */
int n2s$net_stop_listeners (void)
{
int	l_rc;

	for (int i = 0; i < g_listeners_nr; i++ )
		{
		if ( 0 > s_pfd_lsnr[i].fd )				/* This one has not been started		*/
			continue;

		$LOG(STS$K_WARN, "[#%d] Listener " IPv4_BYTES_FMT ":%d --- is aborted", s_pfd_lsnr[i].fd,
					 IPv4_BYTES(g_listeners[i].sk.sin_addr.s_addr), ntohs(g_listeners[i].sk.sin_port) );

		close(s_pfd_lsnr[i].fd);
		s_pfd_lsnr[i].fd = -1;
		}

	/*
	 * The serial devices are closed over the table of serials: a few listeners are allowed to
	 * share a single device, so a per-listener close would try to close it more than once.
	 */
	for (int i = 0; i < g_serials_nr; i++ )
		{
		if ( g_serials[i].state == N2S$K_STATE_IDLE )
			continue;

		if ( !(1 & (l_rc = n2s$tty_close (&g_serials[i]))) )
			$LOG(STS$K_ERROR, "Error close target <%s>", g_serials[i].devname);
		}

	return	STS$K_SUCCESS;
}


/*
 *   DESCRIPTION: Start the network subsystem: for every record of the listeners table open a
 *	TCP socket, bind it to the has been configured address:port, open the target serial
 *	device, publish the socket to the poll() array of the dispatcher and finally start the
 *	dispatcher thread. A listener with a dead target device is skipped with a diagnostic.
 *
 *   INPUTS:
 *	NONE
 *
 *   OUTPUTS:
 *	NONE
 *
 *   IMPLICITE INPUTS:
 *	g_listeners, g_listeners_nr
 *
 *   IMPLICITE OUTPUTS:
 *	s_pfd_lsnr
 *
 *   RETURNS:
 *	condition code; STS$K_ERROR - no one listener has been started
 */
int n2s$net_start_listeners (void)
{
int	l_rc = STS$K_ERROR, l_sd, l_one = 1, l_count;
N2S$_LISTENER	*l_listener;
const socklen_t l_slen = sizeof(struct sockaddr_in);
pthread_t	l_tid;


	for (int i = 0; i < N2S$K_MAX_LISTENERS; i++)			/* poll() ignores a negative descriptor		*/
		s_pfd_lsnr[i].fd = -1;

	l_count = 0;

	for (int i = 0; i < g_listeners_nr; i++)
		{
		l_listener = &g_listeners[i];


		if ( 0 > (l_sd = socket(AF_INET, SOCK_STREAM, 0)) )
			return	$LOG(STS$K_ERROR, "socket()->%d, errno: %d", l_sd, errno);

		if( 0 > setsockopt(l_sd, SOL_SOCKET, SO_REUSEADDR, (char *)&l_one, sizeof(l_one))  )
			$LOG(STS$K_WARN, "setsockopt(%d, SO_REUSEADDR), errno: %d", l_sd, errno);

		if( 0 > setsockopt(l_sd, SOL_SOCKET, SO_REUSEPORT, (char *)&l_one, sizeof(l_one))  )
			$LOG(STS$K_WARN, "setsockopt(%d, SO_REUSEPORT), errno: %d", l_sd, errno);

		if ( 0 > bind(l_sd, (struct sockaddr*) &l_listener->sk, l_slen) )
			{
			close(l_sd);
			$PUTMSG_FAO(N2S$__LSNRERR, &l_listener->sk.sin_addr, ntohs(l_listener->sk.sin_port),
				"bind()", errno);

			continue;
			}

		if ( 0 > (l_rc = listen(l_sd, l_listener->connlm)) )
			{
			close(l_sd);
			$PUTMSG_FAO(N2S$__LSNRERR, &l_listener->sk.sin_addr, ntohs(l_listener->sk.sin_port),
				"listen()", errno);

			continue;
			}

		/*
		 * A target device is opened BEFORE the socket is published to the poll() array:
		 * we do not want to accept clients for a leg which does not work.
		 */
		if ( !(1 & (l_rc = n2s$tty_open (l_listener->serial))) )
			{
			close(l_sd);					/* Don't leak the descriptor			*/
			$LOG(STS$K_ERROR, "Error open target <%s>", l_listener->serial->devname);

			continue;
			}


		l_listener->fd = l_sd;
		s_pfd_lsnr[i].fd = l_sd;
		s_pfd_lsnr[i].events = POLLIN;

		l_count++;

		$PUTMSG_FAO(N2S$__LSNRRDY, s_pfd_lsnr[i].fd, &l_listener->sk.sin_addr,
			ntohs(l_listener->sk.sin_port), l_listener->serial->devname);
		}


	if ( !l_count )
		return	$LOG(STS$K_ERROR, "No listeners has been started!");


	if ( (l_rc = pthread_create(&l_tid, NULL, s_net_listener, NULL)) )
		return	$LOG(STS$K_ERROR, "Cannot start network listener thread, pthread_create()->%d, errno: %d", l_rc, errno);



	return	STS$K_SUCCESS;
}
