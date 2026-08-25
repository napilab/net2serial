#define	__MODULE__	"N2S-TTY"
#define	__IDENT__	"X.00-05"
#define	__REV__		"00.05.00"

/*
**++
**
**  FACILITY: A yet another Network to Serial gateway
**
**  ENVIRONMENT: Linux
**
**  DESCRIPTION: This module covers the TTY related functionality: the serial line open and
**	initialization, the read/write of the octets stream, the flow control and the ownership
**	of the line between the network sessions.
**
**  AUTHORS: StarLet Squad and Ruslan R. Laishev (AKA: BadAss sysman)
**
**  CREATION DATE:  26-SEP-2025
**
**  MODIFICATION HISTORY:
**
**	25-AUG-2026	RRL	X.00-05 / REV: 00.05.00 - Audit fixes:
**				__MODULE__ is "N2S-TTY" now (was the alien "T2R-TTY");
**				n2s$tty_tx(): a partial write() does not throw the tail of the data away
**				anymore - the ring is advanced by the really written count, EAGAIN and
**				EINTR are a normal flow now (the UART FIFO is full - just come back later);
**				n2s$tty_rx(): EAGAIN is detected by errno (was: by the return value, so the
**				branch was dead and every EAGAIN went to the ERROR log);
**				s_tty_speed2bits(): fixed an undeclared <tspeed> in the five branches,
**				the routine returns a status and the value via an output parameter now;
**				s_tty_open(): TIOCSRS485 (was TIOCGRS485 - the RS485 mode was never set),
**				the flow control is really applied (was: declared in the settings and
**				ignored), the descriptor is not leaked on any error path;
**				s_tty_close(): the descriptor is closed even when isatty() has failed;
**				the open/close lock is replaced by the acquire/release ownership pair -
**				the mutex is not held across the threads anymore (an unlock by a foreign
**				thread is an undefined behaviour);
**				n2s$tty_flush(): fixed the deadline arithmetic (the routine was building
**				a deadline of a doubled uptime), is exported and is used at the session
**				start now; removed the dead code: th_func_t, <linux/i2c.h>, the unused
**				externals of the serials table.
**
**--
*/
#include	<time.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<errno.h>
#include	<unistd.h>
#include	<poll.h>
#include	<termios.h>
#include	<fcntl.h>
#include	<sys/uio.h>
#include	<linux/serial.h>
#include	<sys/ioctl.h>


#define		__FAC__	"NET2SER"
#define		__TFAC__ __FAC__ ": "					/* Special prefix for $TRACE			*/

#include	"utility_routines.h"
#include	"defs.h"
#include	"msgs.h"



extern int	g_exit_flag, g_trace;




/*
 *   DESCRIPTION: Give the serial line to the calling session. Only one session at a time is
 *	allowed to work with a line: an octets stream has no framing, so a second client would
 *	interleave its octets with the octets of the first one in the both directions.
 *
 *   INPUTS:
 *	a_serial:	A context of the serial line
 *	a_sd:		A socket descriptor of the session which asks for the line
 *
 *   OUTPUTS:
 *	a_serial:	<owner_sd> is set to <a_sd> on success
 *	a_owner_sd:	A socket of the session which owns the line now (on the busy return)
 *
 *   RETURNS:
 *	condition code; STS$K_WARN - the line is busy with another session
 */
int	n2s$tty_acquire (
		N2S$_SERIAL	*a_serial,
			int	a_sd,
			int	*a_owner_sd
		)
{
int	l_rc;

	*a_owner_sd = -1;

	if ( (l_rc = pthread_mutex_lock(&a_serial->lock)) )
		return	$LOG(STS$K_ERROR, "pthread_mutex_lock(<%s>)->%d, errno: %d", a_serial->devname, l_rc, errno);

	if ( 0 <= a_serial->owner_sd )					/* Somebody is working with the line already	*/
		{
		*a_owner_sd = a_serial->owner_sd;
		l_rc = STS$K_WARN;
		}
	else	{
		a_serial->owner_sd = a_sd;
		l_rc = STS$K_SUCCESS;
		}

	if ( (pthread_mutex_unlock(&a_serial->lock)) )
		$LOG(STS$K_ERROR, "pthread_mutex_unlock(<%s>), errno: %d", a_serial->devname, errno);

	return	l_rc;
}


/*
 *   DESCRIPTION: Release the serial line which has been taken by n2s$tty_acquire(). A release
 *	by a session which does not own the line is refused - it would give the line away while
 *	the real owner is still working with it.
 *
 *   INPUTS:
 *	a_serial:	A context of the serial line
 *	a_sd:		A socket descriptor of the session which releases the line
 *
 *   OUTPUTS:
 *	a_serial:	<owner_sd> is set to -1 on success
 *
 *   RETURNS:
 *	condition code
 */
int	n2s$tty_release (
		N2S$_SERIAL	*a_serial,
			int	a_sd
		)
{
int	l_rc;

	if ( (l_rc = pthread_mutex_lock(&a_serial->lock)) )
		return	$LOG(STS$K_ERROR, "pthread_mutex_lock(<%s>)->%d, errno: %d", a_serial->devname, l_rc, errno);

	if ( a_serial->owner_sd == a_sd )
		a_serial->owner_sd = -1, l_rc = STS$K_SUCCESS;
	else	l_rc = $LOG(STS$K_WARN, "[#%d] Is not an owner of <%s> (owner: #%d) --- release is ignored",
			a_sd, a_serial->devname, a_serial->owner_sd);

	if ( (pthread_mutex_unlock(&a_serial->lock)) )
		$LOG(STS$K_ERROR, "pthread_mutex_unlock(<%s>), errno: %d", a_serial->devname, errno);

	return	l_rc;
}


/*
 *   DESCRIPTION: Send a next contiguous chunk of the data from the ring buffer to the serial
 *	device. A single write() is performed: a partial write is a normal case on a slow line,
 *	so the ring is advanced by the really written count and the rest is sent at the next call.
 *
 *   INPUTS:
 *	a_serial:	A context of the serial line
 *
 *   OUTPUTS:
 *	a_buf_dsc:	The ring buffer, is advanced by the count of the has been sent octets
 *
 *   RETURNS:
 *	condition code
 */
int	n2s$tty_tx (
	const N2S$_SERIAL	*a_serial,
		N2S$_RING	*a_buf_dsc
		)
{
int	l_rc, l_len;
char	*l_data;

	if ( !(l_len = n2s$_ring_getdata (a_buf_dsc, &l_data)) )
		return	STS$K_SUCCESS;					/* Nothing to be sent - just return		*/

	if ( 0 > (l_rc = write(a_serial->fd, l_data, l_len)) )
		{
		/*
		 * The port is opened with O_NDELAY: a full UART FIFO is reported as EAGAIN and is
		 * a normal flow - the data stays in the ring and will be sent at the next round.
		 */
		if ( (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR) )
			return	STS$K_SUCCESS;

		return	$LOG(STS$K_ERROR, "[#%d:<%s>] Xmit of %d octets failed, write()->%d, errno: %d",
				a_serial->fd, a_serial->devname, l_len, l_rc, errno);
		}

	n2s$_ring_adjdata(a_buf_dsc, l_rc);				/* Advance by the REALLY written count !	*/

	$IFTRACE(g_trace, "[#%d:<%s>] Sent %d octets (from %d)", a_serial->fd, a_serial->devname, l_rc, l_len);

	return	STS$K_SUCCESS;
}


/*
 *   DESCRIPTION: Read a next portion of the data from the serial line into the ring buffer.
 *	A single read() is performed into the contiguous free chunk of the ring.
 *
 *   INPUTS:
 *	a_serial:	A context of the serial line
 *
 *   OUTPUTS:
 *	a_buf_dsc:	The ring buffer, is advanced by the count of the has been read octets
 *
 *   RETURNS:
 *	condition code
 */
int	n2s$tty_rx (
	const N2S$_SERIAL	*a_serial,
		N2S$_RING	*a_buf_dsc
		)
{
int	l_rc, l_len;
char	*l_data;


	if ( !(l_len = n2s$_ring_getfree (a_buf_dsc, &l_data)) )
		return	STS$K_SUCCESS;					/* No free space - just return			*/


	if ( 0 > (l_rc = read(a_serial->fd, l_data, l_len)) )
		{
		/*
		 * EAGAIN here means "no data at this moment" and is NOT an error: it is the normal
		 * answer of a port which has been opened with O_NDELAY.
		 */
		if ( (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR) )
			return	STS$K_SUCCESS;

		return	$LOG(STS$K_ERROR, "[#%d:<%s>] Read failed, read()->%d, errno: %d",
				a_serial->fd, a_serial->devname, l_rc, errno);
		}

	n2s$_ring_adjfree(a_buf_dsc, l_rc);				/* Adjust ring's buffer internals		*/


	$IFTRACE(g_trace, "[#%d:<%s>] Read %d octets", a_serial->fd, a_serial->devname, l_rc);

	return	STS$K_SUCCESS;

}


/*
 *   DESCRIPTION: Translate a line rate in bauds to the termios internal representative.
 *
 *   INPUTS:
 *	a_speed:	A data speed in bauds
 *
 *   OUTPUTS:
 *	a_bits:		The termios speed constant (B9600 on a failure)
 *
 *   RETURNS:
 *	condition code; STS$K_WARN - the rate is not supported by the platform
 */
static	int s_tty_speed2bits (
			int	a_speed,
			speed_t	*a_bits
		)
{
	switch (a_speed)
		{
		#if defined(B50)
		case 50:	*a_bits = B50;		break;
		#endif

		#if defined(B75)
		case 75:	*a_bits = B75;		break;
		#endif

		#if defined(B110)
		case 110:	*a_bits = B110;		break;
		#endif

		#if defined(B134)
		case 134:	*a_bits = B134;		break;
		#endif

		#if defined(B150)
		case 150:	*a_bits = B150;		break;
		#endif

		#if defined(B200)
		case 200:	*a_bits = B200;		break;
		#endif

		#if defined(B300)
		case 300:	*a_bits = B300;		break;
		#endif

		#if defined(B600)
		case 600:	*a_bits = B600;		break;
		#endif

		#if defined(B1200)
		case 1200:	*a_bits = B1200;	break;
		#endif

		#if defined(B1800)
		case 1800:	*a_bits = B1800;	break;
		#endif

		#if defined(B2400)
		case 2400:	*a_bits = B2400;	break;
		#endif

		#if defined(B4800)
		case 4800:	*a_bits = B4800;	break;
		#endif

		#if defined(B7200)
		case 7200:	*a_bits = B7200;	break;
		#endif

		#if defined(B9600)
		case 9600:	*a_bits = B9600;	break;
		#endif

		#if defined(B12000)
		case 12000:	*a_bits = B12000;	break;
		#endif

		#if defined(B14400)
		case 14400:	*a_bits = B14400;	break;
		#endif

		#if defined(B19200)
		case 19200:	*a_bits = B19200;	break;
		#elif defined(EXTA)
		case 19200:	*a_bits = EXTA;		break;
		#endif

		#if defined(B38400)
		case 38400:	*a_bits = B38400;	break;
		#elif defined(EXTB)
		case 38400:	*a_bits = EXTB;		break;
		#endif

		#if defined(B57600)
		case 57600:	*a_bits = B57600;	break;
		#endif

		#if defined(B115200)
		case 115200:	*a_bits = B115200;	break;
		#endif

		#if defined(B230400)
		case 230400:	*a_bits = B230400;	break;
		#endif

		#if defined(B460800)
		case 460800:	*a_bits = B460800;	break;
		#endif

		#if defined(B500000)
		case 500000:	*a_bits = B500000;	break;
		#endif

		#if defined(B576000)
		case 576000:	*a_bits = B576000;	break;
		#endif

		#if defined(B921600)
		case 921600:	*a_bits = B921600;	break;
		#endif

		#if defined(B1000000)
		case 1000000:	*a_bits = B1000000;	break;
		#endif

		#if defined(B1152000)
		case 1152000:	*a_bits = B1152000;	break;
		#endif

		#if defined(B1500000)
		case 1500000:	*a_bits = B1500000;	break;
		#endif

		#if defined(B2000000)
		case 2000000:	*a_bits = B2000000;	break;
		#endif

		#if defined(B2500000)
		case 2500000:	*a_bits = B2500000;	break;
		#endif

		#if defined(B3000000)
		case 3000000:	*a_bits = B3000000;	break;
		#endif

		#if defined(B3500000)
		case 3500000:	*a_bits = B3500000;	break;
		#endif

		#if defined(B4000000)
		case 4000000:	*a_bits = B4000000;	break;
		#endif

		default:
			*a_bits = B9600;
			return	$LOG(STS$K_WARN, "Cannot translate speed %d baud to internal representative, set 9600", a_speed);
		}

	return	STS$K_SUCCESS;
}


/*
 *   DESCRIPTION: Translate a flow control constant to a human readable mnemonic for the
 *	diagnostic messages.
 *
 *   INPUTS:
 *	a_flow:		A flow control discipline, see the N2S$K_FLOW_* constants
 *
 *   OUTPUTS:
 *	NONE
 *
 *   RETURNS:
 *	An address of the ASCIIZ mnemonic string
 */
const char * n2s$tty_flow2str (
			int	a_flow
		)
{
	switch ( a_flow )
		{
		case	N2S$K_FLOW_NONE:	return	"NONE";
		case	N2S$K_FLOW_XONXOFF:	return	"XON/XOFF";
		case	N2S$K_FLOW_RTSCTS:	return	"RTS/CTS";
		}

	return	"UNKNOWN";
}


/*
 *   DESCRIPTION: Close the serial device: restore the terminal attributes which has been saved
 *	at the open time, close the descriptor. A not opened device is silently ignored.
 *
 *   INPUTS:
 *	a_serial:	A context of the serial line
 *
 *   OUTPUTS:
 *	a_serial:	<fd> is set to -1
 *
 *   RETURNS:
 *	condition code
 */
static int s_tty_close (
	N2S$_SERIAL	*a_serial
		)
{
int	l_rc, l_sts = STS$K_SUCCESS;

	if (a_serial->fd < 0)
		return	STS$K_SUCCESS;

	/*
	 * A non-tty descriptor is still OUR descriptor: it must be closed anyway, otherwise
	 * it leaks and the next open of the device gets a second one.
	 */
	if ( !(l_rc = isatty(a_serial->fd)) )
		l_sts = $LOG(STS$K_WARN, "isatty(%d)->%d --- invalid I/O descriptor for <%s>, errno: %d",
				a_serial->fd, l_rc, a_serial->devname, errno);
	else if ( 0 > (l_rc = tcsetattr(a_serial->fd, TCSAFLUSH, &a_serial->savedtios)))
		l_sts = $LOG(STS$K_ERROR, "Error tcsetattr(%s)->%d, errno: %d", a_serial->devname, l_rc, errno);

	close(a_serial->fd);
	a_serial->fd = -1;


	return	l_sts;
}


/*
 *   DESCRIPTION: Open and set up a serial port: the raw 8-bit I/O, the has been configured
 *	rate/data bits/parity/stop bits, the flow control discipline and an optional RS-485 mode.
 *	Stolen and adopted from LIBMODBUS\MODBUS-RTU.C.
 *
 *   INPUTS:
 *	a_serial:	A context of the serial line; <devname> and the line parameters (<baud>,
 *			<databits>, <parity>, <stopbits>, <flow>, <flags>) must be filled by the caller
 *
 *   OUTPUTS:
 *	a_serial:	<fd> - a descriptor of the has been opened device (-1 on a failure),
 *			<savedtios> - the original terminal attributes to be restored at the close
 *
 *   RETURNS:
 *	condition code
 */
static int s_tty_open (
			       N2S$_SERIAL	*a_serial
			       )
{
speed_t l_speed = B9600;
int	l_flags = 0, l_rc;

	l_flags = O_RDWR | O_NOCTTY | O_NDELAY | O_EXCL;

#ifdef O_CLOEXEC
	l_flags |= O_CLOEXEC;
#endif

	if ( 0 > (a_serial->fd = open(a_serial->devname, l_flags)) )
		return	a_serial->fd = -1, $PUTMSG_FAO(N2S$__DEVOPNERR, a_serial->devname, errno);

	memset(&a_serial->savedtios, 0, sizeof(struct termios));

	if ( (l_rc = tcgetattr(a_serial->fd , &a_serial->savedtios)) )
		return	close(a_serial->fd), a_serial->fd = -1,
			$LOG(STS$K_ERROR, "Error tcgetattr(%s)->%d, errno: %d", a_serial->devname, l_rc, errno);

	a_serial->tios = a_serial->savedtios;					/* Make local copy */



	/*
	 * Set the baud rate
	 */
	s_tty_speed2bits (a_serial->baud, &l_speed);				/* Translate bauds to internal representative */

	if ( 0 > ( l_rc = cfsetispeed(&a_serial->tios, l_speed)) )		/* Set line rate */
		return	close(a_serial->fd ), a_serial->fd = -1,
			$LOG(STS$K_ERROR, "Error cfsetispeed(%s, %d(%#x))->%d, errno: %d",
				a_serial->devname, a_serial->baud, l_speed, l_rc, errno);

	if ( 0 > ( l_rc = cfsetospeed(&a_serial->tios, l_speed)) )		/* Set line rate */
		return	close(a_serial->fd ), a_serial->fd = -1,
			$LOG(STS$K_ERROR, "Error cfsetospeed(%s, %d(%#x))->%d, errno: %d",
				a_serial->devname, a_serial->baud, l_speed, l_rc, errno);


	/* C_CFLAG      Control options
	   CLOCAL       Local line - do not change "owner" of port
	   CREAD        Enable receiver
	*/
	a_serial->tios.c_cflag |= (CREAD | CLOCAL);

	/* Set data bits (5, 6, 7, 8 bits)
	CSIZE        Bit mask for data bits
	*/
	a_serial->tios.c_cflag &= ~CSIZE;

	switch (a_serial->databits)						/* Translate databits to internal representative */
		{
		case 5:	a_serial->tios.c_cflag |= CS5;	break;
		case 6:	a_serial->tios.c_cflag |= CS6;	break;
		case 7:	a_serial->tios.c_cflag |= CS7;	break;

		case 8:
		default:
		a_serial->tios.c_cflag |= CS8;	break;
		}

	/* Stop bit (1 or 2) */
	if (a_serial->stopbits == 1)
		a_serial->tios.c_cflag &=~ CSTOPB;	/* 1 */
	else	a_serial->tios.c_cflag |= CSTOPB;	/* 2 */


	/* PARENB       Enable parity bit
	   PARODD       Use odd parity instead of even */
	if (a_serial->parity == 'N')
		{ /* None */ a_serial->tios.c_cflag &=~ PARENB;   }
	else if (a_serial->parity == 'E')
		{/* Even */
		a_serial->tios.c_cflag |= PARENB;
		a_serial->tios.c_cflag &=~ PARODD;
		}
	else	{ /* Odd */
		a_serial->tios.c_cflag |= PARENB;
		a_serial->tios.c_cflag |= PARODD;
		}

	if (a_serial->parity == 'N')
		a_serial->tios.c_iflag &= ~INPCK;
	else	a_serial->tios.c_iflag |= INPCK;


	/* Raw input */
	a_serial->tios.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

	/*
	 * The flow control discipline: the both kinds are switched off first, then the has been
	 * configured one is switched on.
	 */
	a_serial->tios.c_iflag &= ~(IXON | IXOFF | IXANY);
	a_serial->tios.c_cflag &= ~CRTSCTS;

	switch ( a_serial->flow )
		{
		case	N2S$K_FLOW_XONXOFF:
			a_serial->tios.c_iflag |= (IXON | IXOFF);
			break;

		case	N2S$K_FLOW_RTSCTS:
			a_serial->tios.c_cflag |= CRTSCTS;
			break;

		case	N2S$K_FLOW_NONE:
		default:
			break;
		}

	/* Raw output */
	a_serial->tios.c_oflag &=~ OPOST;

	/* Unused because we use open with the NDELAY option */
	a_serial->tios.c_cc[VMIN] = 0;
	a_serial->tios.c_cc[VTIME] = 0;

	if ( tcsetattr(a_serial->fd, TCSANOW, &a_serial->tios) < 0 )
		return	close(a_serial->fd), a_serial->fd = -1,
			$LOG(STS$K_ERROR, "tcsetattr <%s>, errno: %d", a_serial->devname, errno);


#ifdef	HAVE_TIOCRS485
	if (a_serial->flags & N2S$M_SERIAL_RS485)
		{
		struct serial_rs485 rs485conf = {0};

		$IFTRACE(g_trace, "Trying to enable RS-485 support for <%s>", a_serial->devname);

		if ( 0 > (l_rc = ioctl(a_serial->fd, TIOCGRS485, &rs485conf)) )
			{
			a_serial->flags &=  ~N2S$M_SERIAL_RS485;
			$LOG(STS$K_WARN, "ioctl(<%s>, TIOCGRS485)->%d, errno: %d --- disabled RS485 support",
				a_serial->devname, l_rc, errno);
			}
		else	{
			rs485conf.flags |= SER_RS485_ENABLED;

			if ( 0 > (l_rc = ioctl(a_serial->fd, TIOCSRS485, &rs485conf)) )	/* SET, not GET !	*/
				{
				a_serial->flags &=  ~N2S$M_SERIAL_RS485;
				$LOG(STS$K_WARN, "ioctl(<%s>, TIOCSRS485)->%d, errno: %d --- disabled RS485 support",
					a_serial->devname, l_rc, errno);
				}
			else	$LOG(STS$K_SUCCESS, "Enabled RS-485 support for <%s>", a_serial->devname);
			}

		}
#endif /* HAVE_TIOCRS485 */

	return	STS$K_SUCCESS;
}


/*
 *   DESCRIPTION: Open and initialize the serial device. Is called once per device at the
 *	start-up time, the device stays open for the whole life of the process: the ownership
 *	between the sessions is arbitrated by n2s$tty_acquire()/n2s$tty_release().
 *
 *   INPUTS:
 *	a_serial:	A context of the serial line
 *
 *   OUTPUTS:
 *	a_serial:	<fd> and <state> are updated
 *
 *   RETURNS:
 *	condition code
 */
int	n2s$tty_open (
			       N2S$_SERIAL	*a_serial
			       )
{
int	l_rc;

	if ( a_serial->state > N2S$K_STATE_IDLE )
		return	$LOG(STS$K_INFO, "Device <%s> --- has been initialized", a_serial->devname);

	if ( !(1 & (l_rc = s_tty_open (a_serial))) )				/* The state is changed ONLY on success	*/
		return	l_rc;

	a_serial->state = N2S$K_STATE_READY;

	$PUTMSG_FAO(N2S$__DEVREADY, a_serial->devname, a_serial->baud, a_serial->databits,
		(a_serial->parity == 'N') ? "N" : ((a_serial->parity == 'E') ? "E" : "O"), a_serial->stopbits,
		n2s$tty_flow2str(a_serial->flow));

	return	l_rc;
}



/*
 *   DESCRIPTION: Close the serial device at the shutdown time.
 *
 *   INPUTS:
 *	a_serial:	A context of the serial line
 *
 *   OUTPUTS:
 *	a_serial:	<fd> and <state> are updated
 *
 *   RETURNS:
 *	condition code
 */
int	n2s$tty_close (
			       N2S$_SERIAL	*a_serial
			       )
{
int	l_rc;

	if ( !a_serial->state )
		return	$LOG(STS$K_WARN, "Device <%s> has not been initialized", a_serial->devname);

	l_rc = s_tty_close (a_serial);
	a_serial->state = N2S$K_STATE_IDLE;
	a_serial->owner_sd = -1;

	return	l_rc;
}



/*
 *   DESCRIPTION: Drop a has been left garbage of the serial line: flush the kernel FIFOs, then
 *	drain whatever the device keeps sending until the line keeps silence. Is called when a
 *	session takes the line, so a new client does not get the tail of the previous dialogue.
 *
 *   INPUTS:
 *	a_serial:	A context of the serial line
 *
 *   OUTPUTS:
 *	NONE
 *
 *   RETURNS:
 *	condition code
 */
int	n2s$tty_flush (
	const N2S$_SERIAL	*a_serial
		)
{
int	l_rc;
struct pollfd	l_pfd = {.fd = a_serial->fd, .events = POLLIN};
struct timespec l_now_ts, l_limit_ts, l_end_input_ts;
char	l_buf[N2S$SZ_BUFF];


	if ( 0 > (l_rc = tcflush(a_serial->fd, TCIOFLUSH)) )
		$LOG(STS$K_WARN, "[#%d:<%s>] tcflush()->%d, errno: %d", a_serial->fd, a_serial->devname, l_rc, errno);

	/*
	 * An upper limit of the draining: one second from now. The limit is built by adding the
	 * interval to the CURRENT time - never by adding the current time to itself.
	 */
	clock_gettime(CLOCK_MONOTONIC, &l_now_ts);

	l_limit_ts.tv_sec = 1;
	l_limit_ts.tv_nsec = 0;
	__util$add_time (&l_now_ts, &l_limit_ts, &l_end_input_ts);

	while ( !g_exit_flag )
		{
		/*
		 * Get current time and check: did we reach the time limit ?
		 *
		 * 	0	- time1 == time2
		 *	0 >	- time1 < time2
		 *	0 <	- time1 > time2
		 */
		clock_gettime(CLOCK_MONOTONIC, &l_now_ts);

		if ( 0 < (l_rc = __util$cmp_time (&l_now_ts, &l_end_input_ts)) )
			break;


		if ( 0 > (l_rc = poll(&l_pfd, 1, 100)) )
			{
			if ( errno == EINTR )
				continue;

			$LOG(STS$K_WARN, "[#%d:<%s>] poll()->%d, errno: %d", a_serial->fd, a_serial->devname, l_rc, errno);
			break;
			}

		if ( !l_rc )
			break;						/* The line keeps silence - we are done		*/

		if ( l_pfd.revents & (POLLERR | POLLHUP | POLLNVAL) )
			break;

		if ( 0 > (l_rc = read(a_serial->fd, l_buf, sizeof(l_buf))) )
			{
			if ( (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR) )
				continue;

			$LOG(STS$K_WARN, "[#%d:<%s>] read()->%d, errno: %d", a_serial->fd, a_serial->devname, l_rc, errno);
			break;
			}
		}

	return	STS$K_SUCCESS;
}
