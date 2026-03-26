#define	__MODULE__	"N2S$-NET"

/*
**++
**
**  FACILITY: A yet another Network to Serial gateway
**
**  ENVIRONMENT: Linux
**
**  DESCRIPTION: A set of routines related to network I/O
**
**  AUTHORS: Ruslan R. (The BadAss Sysman) Laishev
**
**  CREATION DATE:  26-SEP-2025
**
**  MODIFICATION HISTORY:
**
**
**--
*/

#include	<time.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<errno.h>
#include	<unistd.h>
#include	<poll.h>
#include	<netinet/tcp.h>


#define		__FAC__	"NET2SER$"
#define		__TFAC__ __FAC__ ": "					/* Special prefix for $TRACE			*/

#include	"utility_routines.h"
#include	"defs.h"
#include	"msgs.h"



extern int	g_exit_flag, g_trace;


extern N2S$_LISTENER	g_listeners[];
extern int		g_listeners_nr;
static struct pollfd	s_pfd_lsnr[N2S$K_MAX_LISTENERS] = {-1};

static const	int	s_one = 1;


static int	s_net_rx (
		N2S$_SESSION	*a_session,
		N2S$_RING	*a_buf_dsc
		)
{
int	l_rc;
char	*l_data;


	if ( !(l_rc = n2s$_ring_getfree(a_buf_dsc, &l_data)) )				/* Get free space for incoming data */
		return	STS$K_SUCCESS;							/* No space ---- just return */



											/* Read a data from socket */
	if ( !(l_rc = recv(a_session->sd, l_data, l_rc, MSG_NOSIGNAL)) )
		return	$LOG(STS$K_WARN, "[#%d] --- peer close connection, errno: %d", a_session->sd, errno);

	if ( 0 > l_rc )									/* Check return status */
		return	$LOG(STS$K_ERROR, "[#%d] --- error during read data, recv()->%d, errno: %d", a_session->sd, l_rc, errno);



	n2s$_ring_adjfree(a_buf_dsc, l_rc);						/* Adjust ring's buffer internals accorind real received data */

	$IFTRACE(g_trace, "[#%d] NET RX %d octets", a_session->sd, l_rc);

	return	STS$K_SUCCESS;
}



static int	s_net_tx (
		N2S$_SESSION	*a_session,
		N2S$_RING	*a_buf_dsc
		)
{
int	l_rc;
char	*l_data;


	if ( !(l_rc = n2s$_ring_getdata (a_buf_dsc, &l_data)) )
		return	STS$K_SUCCESS;

	if ( !(l_rc = send(a_session->sd, l_data, l_rc, MSG_NOSIGNAL)) )
		return	$LOG(STS$K_WARN, "[#%d] --- peer close connection, errno: %d", a_session->sd, errno);

	if ( 0 > l_rc )									/* Check return status */
		return	$LOG(STS$K_ERROR, "[#%d] --- error during send data, send()->%d, errno: %d", a_session->sd, l_rc, errno);


	n2s$_ring_adjdata(a_buf_dsc, l_rc);						/* Adjust ring's buffer internals */

	$IFTRACE(g_trace, "[#%d] NET TX %d octets", a_session->sd, l_rc);
	//$DUMPHEX(l_data, l_rc);

	return	STS$K_SUCCESS;
}







/*
 *   DESCRIPTION: A worker to serves single TCP-client:
 *	- read from socket data
 *	- send data to serial
 *	- read from serial octets
 *	- send to socket data
 *	- performs error handling and terminate session if need
 *
 *   INPUTS:
 *	a_session:	A session context
 *
 *   OUTPUTS:
 *	NONE:
 *
 *   RETURNS:
 *	NONE
 */

static void *	s_net_session( void *a_arg)
{
int	l_rc;
enum {PFD$K_NET = 0, PFD$K_TTY, PFD$K_MAX};
struct pollfd l_pfd[PFD$K_MAX] = {0};
N2S$_SESSION	*l_session = a_arg;
struct timespec	l_now;
N2S$_SERIAL	*l_serial;
char	*l_data;
N2S$RING_DECL	(l_netbuf_dsc, N2S$SZ_BUFF);
N2S$RING_DECL	(l_ttybuf_dsc, N2S$SZ_BUFF);

	l_session = a_arg;
	assert ( l_session );
	l_serial = l_session->target;
	assert ( l_serial );


	l_session->state = N2S$K_STATE_READY;
	l_session->netbuf_dsc = &l_netbuf_dsc;
	l_session->ttybuf_dsc = &l_ttybuf_dsc;

	l_pfd[PFD$K_NET] = (struct pollfd) {.fd = l_session->sd, .events = POLLIN};
	l_pfd[PFD$K_TTY] = (struct pollfd) {.fd = l_serial->fd, .events = POLLIN};


	clock_gettime(CLOCK_MONOTONIC_COARSE, &l_session->lastio_ts);

	$LOG(STS$K_INFO, "[#%d] Start session for " IPv4_BYTES_FMT ":%d ....", l_session->sd,
		 IPv4_BYTES(l_session->sk.sin_addr.s_addr), ntohs(l_session->sk.sin_port));


	while ( !g_exit_flag )
		{
		/*
		 * Check session timeout and expiration
		 */
		clock_gettime(CLOCK_MONOTONIC_COARSE, &l_now);

		if ( l_session->state == N2S$K_STATE_READY )				/* Check global session timeout for idle session */
			{
			if ( (l_session->lastio_ts.tv_sec + N2S$K_IDLE_TMO_SEC) < l_now.tv_sec)	/* 1200 secs for idle session */
				{
				$LOG(STS$K_WARN, "[#%d] No activity for past %d seconds", l_session->sd, N2S$K_IDLE_TMO_SEC);
				break;
				}
			}

		if ( 0 > (l_rc = poll(l_pfd, PFD$K_MAX, 3000)) )
			{
			$LOG(STS$K_ERROR, "[#%d] poll()->%d, errno: %d", l_session->sd, l_rc, errno);
			continue;
			}
		else if ( !l_rc )
			l_pfd[PFD$K_NET].events = l_pfd[PFD$K_TTY].events = POLLIN;




		if ( l_pfd[PFD$K_NET].revents & POLLIN )				/* Incoming data from network ? */
			{
			if ( (l_rc = n2s$_ring_getfree(&l_netbuf_dsc, &l_data)) )	/* Is there free space in network buffer ? */
				{
				if ( 1 & (l_rc = s_net_rx (l_session, &l_netbuf_dsc)) )	/* Read data from socket */
					l_pfd[PFD$K_TTY].events |= POLLOUT;		/* Set serial to send data */
				else	break;
				}
			else	l_pfd[PFD$K_NET].events &= (~POLLIN);			/* Disable wake up on incomming data - we have't free space in network buffer */
			}

		if ( l_pfd[PFD$K_NET].revents & POLLOUT )				/* Socket is ready to send data over network ? */
			{
			if ( (l_rc = n2s$_ring_getdata(&l_ttybuf_dsc, &l_data)) )	/* Is there data has been received from serial ? */
				{
				if ( 1 & (l_rc = s_net_tx (l_session, &l_ttybuf_dsc )) )/* Send data over network */
					l_pfd[PFD$K_TTY].events |= POLLIN;		/* Set serial is ready for incomming data */
				else	break;
				}
			else	l_pfd[PFD$K_NET].events &= (~POLLOUT);			/* Disable wake up on incomming data - we have't data to be sent */
			}


		if ( l_pfd[PFD$K_TTY].revents & POLLIN )
			{
			if ( (l_rc = n2s$_ring_getfree(&l_ttybuf_dsc, &l_data)) )
				{
				if ( 1 & (l_rc = n2s$tty_rx (l_serial, &l_ttybuf_dsc)) )
					l_pfd[PFD$K_NET].events |= POLLOUT;
				}
			else	l_pfd[PFD$K_TTY].events &= (~POLLIN);
			}


		if ( l_pfd[PFD$K_TTY].revents & POLLOUT )
			{
			if ( (l_rc = n2s$_ring_getdata(&l_netbuf_dsc, &l_data)) )
				{
				if ( 1 & (l_rc = n2s$tty_tx (l_serial, &l_netbuf_dsc)) )
					l_pfd[PFD$K_NET].events |= POLLIN;
				}
			else	l_pfd[PFD$K_TTY].events &= (~POLLOUT);
			}
		}



	$LOG(STS$K_INFO, "[#%d] Close session for " IPv4_BYTES_FMT ":%d", l_session->sd,
		 IPv4_BYTES(l_session->sk.sin_addr.s_addr), ntohs(l_session->sk.sin_port));


	close(l_session->sd);
	free( (void *) l_session);

}





void *	s_net_listener( void *a_arg)
{
int	l_rc, l_sd;
struct sockaddr_in	l_sk = {0};
socklen_t l_slen = sizeof(struct sockaddr);
N2S$_SESSION	*l_session;
pthread_t	l_tid;

	while ( !g_exit_flag )
		{
		/*
		 * Wait for any new TCP-connection request on all ports ...
		 */
		if ( 0 > (l_rc = poll(s_pfd_lsnr, g_listeners_nr, 5000)) )
			$LOG(STS$K_ERROR, "poll()->%d, errno: %d", l_rc, errno);


		/*
		 * Run over all listeners and check for POLLIN (TCP-connection request)
		 */
		for (int i = 0; i < g_listeners_nr; i++ )
			{
			if ( s_pfd_lsnr[i].revents & POLLIN )				/* Is there any connection request ? */
				{							/* Accept TCP connection */
				if ( 0 > (l_sd = accept(s_pfd_lsnr[i].fd, (struct sockaddr *)&l_sk, &l_slen)) < 0)
					$LOG(STS$K_ERROR, "accept(%d)->%d, errno: %d", s_pfd_lsnr[i].fd, l_rc, errno);
				else	{						/* Create new session context */
					$IFTRACE(g_trace, "[#%d] Accept connection from " IPv4_BYTES_FMT ":%d on SD: #%d", s_pfd_lsnr[i].fd,
						 IPv4_BYTES(l_sk.sin_addr.s_addr), ntohs(l_sk.sin_port), l_sd);

					if ( l_rc = setsockopt(l_sd, IPPROTO_TCP, TCP_NODELAY, (char *) &s_one, sizeof(s_one)) )
						$LOG(STS$K_WARN, "setsockopt()->%d, errno=%d", l_rc, errno);


					l_session = calloc(1, sizeof(N2S$_SESSION));

					l_session->sd = l_sd;
					l_session->sk = l_sk;
					l_session->target = g_listeners[i].serial;

											/* Start dedicated thread for session */
					if ( l_rc = pthread_create(&l_tid, NULL, s_net_session, l_session) )
						{
						$LOG(STS$K_ERROR, "Cannot start network session thread, pthread_create()->%d, errno=%d", l_rc, errno);
						free(l_session);
						}
					}
				}
			}
		}

}



int n2s$net_stop_listeners (void)
{
int	l_rc;

	for (int i = 0; i < g_listeners_nr; i++ )
		{
		close(s_pfd_lsnr[i].fd);
		$LOG(STS$K_WARN, "[#%d] Listener " IPv4_BYTES_FMT ":%d --- is aborted", s_pfd_lsnr[i].fd,
					 IPv4_BYTES(g_listeners[i].sk.sin_addr.s_addr), ntohs(g_listeners[i].sk.sin_port) );


		if ( !(1 & (l_rc = n2s$tty_close (g_listeners[i].serial))) )
			$LOG(STS$K_ERROR, "Error close  target <%s>", g_listeners[i].serial->devname);
		}

	return	STS$K_SUCCESS;
}


int n2s$net_start_listeners (void)
{
int	l_rc, l_sd, l_one = 1, l_count;
N2S$_LISTENER	*l_listener;
const socklen_t l_slen = sizeof(struct sockaddr);
pthread_t	l_tid;


	l_count = 0;

	for (int i = 0; i < g_listeners_nr; i++)
		{
		l_listener = &g_listeners[i];


		if ( 0 > (l_sd = socket(AF_INET, ((l_listener->proto == IPPROTO_UDP) ? SOCK_DGRAM : SOCK_STREAM), 0)) )
				return	$LOG(STS$K_ERROR, "socket()->%d, errno=%d", l_sd, errno);

		if( 0 > setsockopt(l_sd, SOL_SOCKET, SO_REUSEADDR, (char *)&l_one, sizeof(l_one))  )
			$LOG(STS$K_WARN, "setsockopt(%d, SO_REUSEADDR), errno=%d", l_sd, errno);

		if( 0 > setsockopt(l_sd, SOL_SOCKET, SO_REUSEPORT, (char *)&l_one, sizeof(l_one))  )
			$LOG(STS$K_WARN, "setsockopt(%d, SO_REUSEPORT), errno=%d", l_sd, errno);

		if ( 0 > bind(l_sd, (struct sockaddr*) &l_listener->sk, l_slen) )
			{
			close(l_sd);
			$LOG(STS$K_ERROR, "bind(%d, " IPv4_BYTES_FMT  ":%d), errno=%d", l_sd,
			     IPv4_BYTES(l_listener->sk.sin_addr.s_addr), ntohs(l_listener->sk.sin_port), errno);

			continue;
			}

		if ( 0 > (l_rc = listen(l_sd, l_listener->connlm)) )
			{
			close(l_sd);
			$LOG(STS$K_ERROR, "listen(%d)->%d, errno: %d",l_sd, l_rc, errno);

			continue;
			}


		l_listener->fd = l_sd;
		s_pfd_lsnr[i].fd = l_sd;
		s_pfd_lsnr[i].events = POLLIN;


		if ( !(1 & (l_rc = n2s$tty_open (l_listener->serial))) )
			{
			$LOG(STS$K_ERROR, "Error open target <%s>", l_listener->serial->devname);

			continue;
			}


		l_count++;

		$LOG(STS$K_SUCCESS, "[#%d] Listener [" IPv4_BYTES_FMT ":%d, Target: <#%d:%s>] --- initialized", s_pfd_lsnr[i].fd,
					 IPv4_BYTES(g_listeners[i].sk.sin_addr.s_addr), ntohs(g_listeners[i].sk.sin_port),
					l_listener->serial->fd, l_listener->serial->devname);
		}


	if ( !l_count )
		return	$LOG(STS$K_ERROR, "No listeners has been started!");


	if ( l_rc = pthread_create(&l_tid, NULL, s_net_listener, NULL) )
		return	$LOG(STS$K_ERROR, "Cannot start network listener thread, pthread_create()->%d, errno=%d", l_rc, errno);



	return	STS$K_SUCCESS;
}
