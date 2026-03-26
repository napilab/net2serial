#define	__MODULE__	"T2R-TTY"

/*
**++
**
**  FACILITY: A yet another Network to Serial gateway
**
**  ENVIRONMENT: Linux
**
**
**  DESCRIPTION: This coule cover TTY related functionality. Serial line open and initialization, read\write and timeout processing.
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
#include	<termios.h>
#include	<fcntl.h>
#include	<termios.h>
#include	<sys/uio.h>
#include	<linux/serial.h>
#include	<sys/ioctl.h>
#include	<linux/i2c.h>


#define		__FAC__	"NET2SER"
#define		__TFAC__ __FAC__ ": "					/* Special prefix for $TRACE			*/

#include	"utility_routines.h"
#include	"defs.h"
#include	"msgs.h"



typedef void * (*th_func_t)(void *);					/* To eliminate pthread_create warning */




extern int	g_exit_flag, g_trace;

extern N2S$_SERIAL	g_serials[];
extern	int		g_serials_nr;



static inline int s_tty_lock (
		N2S$_SERIAL	*a_serial
		)
{
struct timespec l_now, l_tmo = {.tv_sec = 13}, l_etime;
int	l_rc;

	for (int i = 0; i < 3; i++)
		{
		if ( l_rc = clock_gettime(CLOCK_REALTIME_COARSE, &l_now) )
			return	$LOG(STS$K_ERROR, "clock_gettime->%d, errno: %d", l_rc, errno);

		__util$add_time (&l_now, &l_tmo, &l_etime);

		if ( !(l_rc = pthread_mutex_timedlock(&a_serial->lock, &l_etime)) )
			{
			//$IFTRACE(g_trace, "Got exclusive access to <%s> ...", a_serial->devname);
			return	STS$K_SUCCESS;
			}

		$LOG(STS$K_WARN, "pthread_mutex_timedlock(<%s>)->%d, errno=%d", a_serial->devname, l_rc, errno);
		}


	return	$LOG(STS$K_ERROR, "Cannot get exclusive access for <%s> after %d attempts", a_serial->devname, 3);
}


static inline int s_tty_unlock (
		N2S$_SERIAL	*a_serial
		)
{
int	l_rc;

		l_rc = pthread_mutex_unlock(&a_serial->lock);
		//$IFTRACE(g_trace, "Release lock for <%s> (errno: %d)", a_serial->devname, l_rc ? errno: 0);

		return	STS$K_SUCCESS;
}


/*
 *   DESCRIPTION: Send data from ring buffer to serial device.
 *
 *   INPUTS:
 *	a_serial:	A context of the serial line
 *	a_out_dsc:	A buffer with the PDU RTU to be sent
 *
 *   OUTPUTS:
 *	NONE
 *
 *   RETURNS:
 *	condition status
 */
int	n2s$tty_tx (
	const N2S$_SERIAL	*a_serial,
		N2S$_RING	*a_buf_dsc
		)
{
int	l_rc, l_len;
char	*l_data;

	if ( !(l_len = n2s$_ring_getdata (a_buf_dsc, &l_data)) )
		return	STS$K_SUCCESS;

	if ( 0 > (l_rc = write(a_serial->fd,l_data, l_len )) )
		return	$LOG(STS$K_ERROR, "[#%d:<%s>] Xmit of %d octets failed, errno: %d", a_serial->fd, a_serial->devname, l_len, errno);

	n2s$_ring_adjdata(a_buf_dsc, l_len),					/* Adjust ring's buffer internals accorind real received data */

	$IFTRACE(g_trace, "[#%d:<%s>] Send %d octets (from %d)", a_serial->fd, a_serial->devname, l_rc, l_len);

	return	STS$K_SUCCESS;
}


/*
 *   DESCRIPTION: Read data from serial line.
 */
int	n2s$tty_rx (
	const N2S$_SERIAL	*a_serial,
		N2S$_RING	*a_buf_dsc

		)
{
int	l_rc;
char	*l_data;


	if ( !(l_rc = n2s$_ring_getfree (a_buf_dsc, &l_data)) )
		return	STS$K_SUCCESS;


	if ( 0 > (l_rc = read(a_serial->fd, l_data, l_rc )) )
		return	(l_rc == EAGAIN) ?
				STS$K_ERROR : $LOG(STS$K_ERROR, "[#%d:<%s>] Read failed, errno: %d", a_serial->fd, a_serial->devname, errno);

	n2s$_ring_adjfree(a_buf_dsc, l_rc);					/* Adjust ring's buffer internals accorind real received data */


	$IFTRACE(g_trace, "[#%d:<%s>] Read %d octets", a_serial->fd, a_serial->devname, l_rc);
	//$DUMPHEX(l_data, l_rc);

	return	STS$K_SUCCESS;

}


/*
 *   DESCRIPTION: Translate line rate to internal representative. In error case return <B9600>
 *
 *   INPUTS:
 *	a_speed:	A data speed in bauds
 *
 *   OUTPUTS:
 *	NONE
 *
 *   RETURNS:
 *	<line_speed>
 */
static	speed_t s_tty_speed2bits(int a_speed)
{
speed_t l_speed;

	switch (a_speed)
		{
		#if defined(B50)
		case 50:	l_speed = B50;		break;
		#endif

		#if defined(B75)
		case 75:	l_speed = B75;		break;
		#endif

		#if defined(B110)
		case 110:	l_speed = B110;		break;
		#endif

		#if defined(B134)
		case 134:	l_speed = B134;		break;
		#endif

		#if defined(B150)
		case 150:	l_speed = B150;		break;
		#endif

		#if defined(B200)
		case 200:	l_speed = B200;		break;
		#endif

		#if defined(B300)
		case 300:	l_speed = B300;		break;
		#endif

		#if defined(B600)
		case 600:	l_speed = B600;		break;
		#endif

		#if defined(B1200)
		case 1200:	l_speed = B1200;	break;
		#endif

		#if defined(B1800)
		case 1800:	l_speed = B1800;	break;
		#endif

		#if defined(B2400)
		case 2400:	l_speed = B2400;	break;
		#endif

		#if defined(B4800)
		case 4800:	l_speed = B4800;	break;
		#endif

		#if defined(B7200)
		case 7200:	tspeed = B7200;		break;
		#endif

		#if defined(B9600)
		case 9600:	l_speed = B9600;	break;
		#endif

		#if defined(B12000)
		case 12000:	tspeed = B12000;	break;
		#endif

		#if defined(B14400)
		case 14400:	tspeed = B14400;	break;
		#endif

		#if defined(B19200)
		case 19200:	l_speed = B19200;	break;
		#elif defined(EXTA)
		case 19200:	tspeed = EXTA;		break;
		#endif

		#if defined(B38400)
		case 38400:	l_speed = B38400;	break;
		#elif defined(EXTB)
		case 38400:	tspeed = EXTB;		break;
		#endif

		#if defined(B57600)
		case 57600:	l_speed = B57600;	break;
		#endif

		#if defined(B115200)
		case 115200:	l_speed = B115200;	break;
		#endif

		#if defined(B230400)
		case 230400:	l_speed = B230400;	break;
		#endif

		#if defined(B460800)
		case 460800:	l_speed = B460800;	break;
		#endif

		#if defined(B500000)
		case 500000:	l_speed = B500000;	break;
		#endif

		#if defined(B576000)
		case 576000:	l_speed = B576000;	break;
		#endif

		#if defined(B921600)
		case 921600:	l_speed = B921600;	break;
		#endif

		#if defined(B1000000)
		case 1000000:	l_speed = B1000000;	break;
		#endif

		#if defined(B1152000)
		case 1152000:	l_speed = B1152000;	break;
		#endif

		#if defined(B1500000)
		case 1500000:	l_speed = B1500000;	break;
		#endif

		#if defined(B2000000)
		case 2000000:	l_speed = B2000000;	break;
		#endif

		#if defined(B2500000)
		case 2500000:	l_speed = B2500000;	break;
		#endif

		#if defined(B3000000)
		case 3000000:	l_speed = B3000000;	break;
		#endif

		#if defined(B3500000)
		case 3500000:	l_speed = B3500000;	break;
		#endif

		#if defined(B4000000)
		case 4000000:	l_speed = B4000000;	break;
		#endif

		default:
			$LOG(STS$K_WARN, "Cannot translate speed %d baud to internal representative, set 9600", a_speed);
			l_speed = B9600;
		}

	return l_speed;
}

static int s_tty_close (
	N2S$_SERIAL	*a_serial
		)
{
int	l_rc;

	if (a_serial->fd < 0)
		return	STS$K_SUCCESS;

	if ( !(l_rc = isatty(a_serial->fd)) )
		return	$LOG(STS$K_WARN, "isatty(%d)->%d --- invalid I/O descriptor for <%s>", a_serial->fd, l_rc, a_serial->devname);

	if ( 0 > (l_rc = tcsetattr(a_serial->fd, TCSAFLUSH, &a_serial->savedtios)))
		$LOG(STS$K_ERROR, "Error tcsetattr(%s)->%d, errno: %d", a_serial->devname, l_rc, errno);

	close(a_serial->fd);
	a_serial->fd = -1;


	return	STS$K_SUCCESS;
}





/*
 *   DESCRIPTION: Sets up a serial port for RTU communications
 *	Stolen and adopted from LIBMODBUS\MODBUS-RTU.C.
 *
 *   INPUTS:
 *	a_serial:	A context for serial comminication line
 *
 *   OUTPUTS:
 *	a_serial:
 *
 *   RETURNS:
 *	condition code
 */
static int s_tty_open (
			       N2S$_SERIAL	*a_serial
			       )
{
speed_t l_speed = -1;
int	l_flags = 0, l_rc;

	l_flags = O_RDWR | O_NOCTTY | O_NDELAY | O_EXCL;

#ifdef O_CLOEXEC
	l_flags |= O_CLOEXEC;
#endif

	if ( 0 > (a_serial->fd = open(a_serial->devname, l_flags)) )
		return	$LOG(STS$K_ERROR, "Can't open the device <%s>, errno: %d", a_serial->devname, errno);

	memset(&a_serial->savedtios, 0, sizeof(struct termios));

	if ( l_rc = tcgetattr(a_serial->fd , &a_serial->savedtios) )
		return	close(a_serial->fd ), $LOG(STS$K_ERROR, "Error tcgetattr(%s)->%d, errno: %d", a_serial->devname, l_rc, errno);

	a_serial->tios = a_serial->savedtios;						/* Make local copy */



	/*
	 * Set the baud rate
	 */
	l_speed = s_tty_speed2bits (a_serial->baud);					/* Translate bauds to internal representative */

	if ( 0 > ( l_rc = cfsetispeed(&a_serial->tios, l_speed)) )			/* Set line rate */
		return	close(a_serial->fd ), a_serial->fd = -1,
			$LOG(STS$K_ERROR, "Error cfsetispeed(%s, %d(%#x))->%d, errno: %d",
				a_serial->devname, a_serial->baud, l_speed, l_rc, errno);

	if ( 0 > ( l_rc = cfsetospeed(&a_serial->tios, l_speed)) )			/* Set line rate */
		return	close(a_serial->fd ), a_serial->fd = -1,
			$LOG(STS$K_ERROR, "Error cfsetospeed(%s, %d(%#x))->%d, errno: %d",
				a_serial->devname, a_serial->baud, l_speed, l_rc, errno);


	/* C_CFLAG      Control options
	   CLOCAL       Local line - do not change "owner" of port
	   CREAD        Enable receiver
	*/
	a_serial->tios.c_cflag |= (CREAD | CLOCAL);

	/* CSIZE, HUPCL, CRTSCTS (hardware flow control) */

	/* Set data bits (5, 6, 7, 8 bits)
	CSIZE        Bit mask for data bits
	*/
	a_serial->tios.c_cflag &= ~CSIZE;

	switch (a_serial->databits)							/* Translate databits to internal representative */
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

	/* Software flow control is disabled */
	a_serial->tios.c_iflag &= ~(IXON | IXOFF | IXANY);

	/* Raw output */
	a_serial->tios.c_oflag &=~ OPOST;

	/* Unused because we use open with the NDELAY option */
	a_serial->tios.c_cc[VMIN] = 0;
	a_serial->tios.c_cc[VTIME] = 0;

	if ( tcsetattr(a_serial->fd, TCSANOW, &a_serial->tios) < 0 )
		return	close(a_serial->fd), a_serial->fd = -1,
			$LOG(STS$K_ERROR, "tcsetattr <%s>, errno: %d", a_serial->devname, errno);


#ifdef HAVE_TIOCRS485
	if (a_serial->flags & N2S$M_SERIAL_RS485)
		{
		struct serial_rs485 rs485conf = {0};

		$IFTRACE(g_trace, "Trying to enable RS-485 support for %s", a_serial->devname);

		if ( 0 > (l_rc = ioctl(a_serial->fd, TIOCGRS485, &rs485conf)) )
			{
			a_serial->flags &=  ~N2S$M_SERIAL_RS485;
			$LOG(STS$K_WARN, "ioctl(<%s>, TIOCGRS485)->%d, errno: %d --- disabled RS485 support", a_serial->devname, l_rc, errno);
			}
		else	{
			rs485conf.flags |= SER_RS485_ENABLED;

			if ( 0 > (l_rc = ioctl(a_serial->fd, TIOCGRS485, &rs485conf)) )
				{
				a_serial->flags &=  ~N2S$M_SERIAL_RS485;
				$LOG(STS$K_WARN, "ioctl(<%s>, TIOCGRS485)->%d, errno: %d --- disabled RS485 support", a_serial->devname, l_rc, errno);
				}
			else	$LOG(STS$K_SUCCESS, "Enabled RS-485 support for %s", a_serial->devname);
			}

		}
#endif /* HAVE_TIOCRS485 */

	return	STS$K_SUCCESS;
}








/*
 *   DESCRIPTION: Get exclusive lock for serial device, check and do if need initialization.
 *
 *   INPUTS:
 *	a_serial:	A context for serial comminication line
 *
 *   OUTPUTS:
 *	a_serial:
 *
 *   RETURNS:
 *	condition code
 */
int	n2s$tty_open (
			       N2S$_SERIAL	*a_serial
			       )
{
int	l_rc;

	if ( !(1 & s_tty_lock (a_serial)) )
		return	$LOG(STS$K_ERROR, "Device <%s> --- cannot be initialized", a_serial->devname);

	if ( a_serial->state > N2S$K_STATE_IDLE )
		l_rc = $LOG(STS$K_INFO, "Device <%s>  --- has been initialized", a_serial->devname);
	else if ( 1 & (l_rc = s_tty_open (a_serial)) )
		  a_serial->state = N2S$K_STATE_READY;
	else	s_tty_unlock (a_serial);

	return	l_rc;
}



/*
 *   DESCRIPTION: Release an exclusive lock of serial device, do close priocedure.
 *
 *   INPUTS:
 *	a_serial:	A context for serial comminication line
 *
 *   OUTPUTS:
 *	a_serial:
 *
 *   RETURNS:
 *	condition code
 */
int	n2s$tty_close (
			       N2S$_SERIAL	*a_serial
			       )
{
int	l_rc;

	s_tty_unlock (a_serial);


	if ( !a_serial->state )
		l_rc = $LOG(STS$K_WARN, "Device <%s>  has not been initialized", a_serial->devname);
	else	l_rc = s_tty_close (a_serial), a_serial->state = N2S$K_STATE_IDLE;

	return	l_rc;
}



/*
 *   DESCRIPTION: Read "rest of serial input data", fluish buffers of serial line.
 *
 *
 *
 */
static int	s_tty_flush (
	const N2S$_SERIAL	*a_serial
		)
{
int	l_rc;
struct pollfd	l_pfd = {.fd = a_serial->fd, .events = POLLIN};
struct timespec l_now_ts, l_end_input_ts = {.tv_sec = 1};
char	l_buf[N2S$SZ_BUFF];


	tcflush(a_serial->fd, TCIOFLUSH);

	clock_gettime(CLOCK_MONOTONIC_COARSE, &l_end_input_ts);
	__util$add_time (&l_end_input_ts, &l_end_input_ts, &l_end_input_ts);

	while ( !g_exit_flag )
		{
		/*
		 * Get current time and check: did we reach a time limit to read whole PDU ?
		 *
		 * 	0	- time1 == time2
		 *	0 >	- time1 < time2
		 *	0 <	- time1 > time2
		 */
		clock_gettime(CLOCK_MONOTONIC_COARSE, &l_now_ts);

		if ( 0 < (l_rc = __util$cmp_time (&l_now_ts, &l_end_input_ts)) )
			break;


		if ( 0 > (l_rc = poll(&l_pfd, 1, 1024)) )
			$LOG(STS$K_WARN, "[#%d:<%s>] poll()->%d, errno: %d", a_serial->fd, a_serial->devname, l_rc, errno);
		else	if ( !l_rc)
			continue;


		if ( 0 > (l_rc = read(a_serial->fd, l_buf, sizeof(l_buf))) )
			{
			$LOG(STS$K_WARN, "[#%d:<%s>] read()->%d, errno: %d", a_serial->fd, a_serial->devname, l_rc, errno);
			break;
			}
		}
}



