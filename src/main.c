#define	__MODULE__	"N2S-MAIN"
#define	__IDENT__	"X.00-06ECO01"
#define	__REV__		"00.06.01"

/*
**++
**
**  FACILITY: A yet another Network to Serial gateway
**
**  ENVIRONMENT: Linux
**
**  ABSTRACT: A server to transparently passing data has been received over TCP-connection to a local serial port.
**
**  DESCRIPTION: Main routine, read and process configuration, start workers.
**
**  AUTHORS: StarLet Squad and Ruslan R. Laishev (AKA: BadAss sysman)
**
**  CREATION DATE:  11-FEB-2026
**
**
**  USAGE:
**	$ net2serial [options]
**		options:
**			/TRACE		- enable extensible debug diagnostic output
**			/LOGFILE	- a file name for the logfile
**			/LOGSIZE	- a limit of logfile in octets
**			/SETTINGS	- configuration option for network and serial stuff
**
**  MODIFICATION HISTORY:
**
**		26-MAR-2026	RRL	X.00-04: Fixed incorrect handling of EAGAIN;
**					improved diagnostic output;
**
**	25-AUG-2026	RRL	X.00-05 / REV: 00.05.00 - Audit fixes:
**				s_settings_process_serials(): flat parsing with an early reject of the
**				illformed records, mandatory <device> and <chars>, the validation of the
**				line parameters with the allowed range in every diagnostic, the flow
**				control is really parsed now, the mutex is initialized for EVERY accepted
**				record (was: only inside the <chars> branch, so a record without <chars>
**				got an uninitialized mutex - an undefined behaviour), the bound check of
**				the table capacity, the libconfig strings are collapsed in a local copy;
**				s_settings_process_listeners(): the same rework - a missing <bind> was a
**				NULL dereference, the sscanf() field widths (a stack overflow from the
**				settings file), the port range and the inet_pton() checks, the UDP
**				transport is rejected explicitly, <target> is stored as a plain ASCIIZ
**				string (was: ASCIC into a char[] field);
**				s_settings_load_n_parse(): a failure of the serials section is not lost;
**				q_settings -> g_settings; the internal routines are static now;
**				s_sig_handler(): the async-signal-safe write() instead of fprintf();
**				main(): returns EXIT_SUCCESS/EXIT_FAILURE (was: a severity code), the
**				banner and the exit status are signalled by the coded FAO messages;
**				the message catalogue got the .facname, so a line looks like
**				"%N2S-E-DEVOPNERR, ..." (was: a doubled facility prefix).
**
**	25-AUG-2026	RRL	X.00-05ECO01 / REV: 00.05.01 - The delivery change (no functional change):
**				README gained the "Who this project is for" section.
**
**	25-AUG-2026	RRL	X.00-05ECO02 / REV: 00.05.02 - The delivery change (no functional change):
**				README is delivered in two languages: README.md + README_RU.md.
**
**	25-AUG-2026	RRL	X.00-06 / REV: 00.06.00 - The build system change (no C source change):
**				the musl C library builds are supported and verified - added the toolchain
**				file cmake/musl.cmake, fixed the linking against a libconfig which lives in
**				a non-standard prefix; the musl build is documented in the both READMEs.
**
**	25-AUG-2026	RRL	X.00-06ECO01 / REV: 00.06.01 - Use the StarLet library facilities where the
**				code was doing the same by hand: $ISINRANGE() for the range validation,
**				$ARRSZ() for the message catalogue size.
**
**--
*/

#include	<signal.h>
#include	<stdint.h>
#include	<time.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<errno.h>
#include	<unistd.h>
#include	<libconfig.h>


#define		__FAC__	"NET2SER"
#define		__TFAC__ __FAC__ ": "					/* Special prefix for $TRACE			*/

#include	"utility_routines.h"
#include	"defs.h"
#include	"msgs.h"


#ifndef	__ARCH__NAME__
#define	__ARCH__NAME__	"VAX"
#endif

#ifdef _DEBUG
	#ifndef	__TRACE__
		#define	__TRACE__
	#endif
#endif // DEBUG


static const	ASC	__ident__ = {$ASCINI(__FAC__ " "  __IDENT__ "/"  __ARCH__NAME__   "(built at "__DATE__ " " __TIME__ " with CC " __VERSION__ ")")},
	__rev__ = {$ASCINI(__REV__)};



static	EMSG_RECORD __n2s_msgs__ [] = {					/* Create array of message records */
	/*
	 * The record text starts with the message abbreviation only: the %<facility>-<severity>-
	 * frame is prefixed by __util$putmsg*() itself (see the .facname below), so the final
	 * output line looks like: %N2S-E-DEVOPNERR, <the record text>
	 */
	#define	$DEF_MSG(f, s, c, t)	{ f##$__##c, 0, { #c ", " t } },
	__DEF_MESSAGES__
	#undef	$DEF_MSG
};


static	EMSG_RECORD_DESC __n2s_msgs_desc__  = {
	.link = NULL,							/* Must beee NULL here */
	.facno = N2S,
	.msgnr = $ARRSZ(__n2s_msgs__),
	.msgrec = __n2s_msgs__,						/* An address of the message records array */
	.facname = "N2S"						/* The %N2S-<S>- framing of __util$putmsg*() */
};


ASC	g_logfspec = {0, {0}},
	g_confspec = {0, {0}},
	g_settings = {0, {0}}
	;


int	g_exit_flag = 0,						/* Global flag 'all must to be stop'	*/
	g_trace = 0;							/* A flag to produce extensible logging	*/

int	g_logsize = 0							/* A size in octets of the log file */
	;



N2S$_SERIAL	g_serials[N2S$K_MAX_SERIALS];				/* A table of serials, filled from configuration at startup time */
int		g_serials_nr;						/* Count of records in the table */

N2S$_LISTENER	g_listeners[N2S$K_MAX_LISTENERS];			/* A table of listeners, filled from configuration at startup time */
int		g_listeners_nr;						/* Count of records in the table */



config_t	g_cfg;							/* Lib Config API context */



static const OPTS g_optstbl [] =					/* General CLI options		*/
{
	{{$ASCINI("config")},	&g_confspec, ASC$K_SZ,		OPTS$K_CONF},
	{{$ASCINI("trace")},	&g_trace, 0,			OPTS$K_OPT},
	{{$ASCINI("logfile")},	&g_logfspec, ASC$K_SZ,		OPTS$K_STR},
	{{$ASCINI("logsize")},	&g_logsize, 0,			OPTS$K_INT},

	{{$ASCINI("settings")},	&g_settings,  ASC$K_SZ,		OPTS$K_STR},

	OPTS_NULL
};



const char	help [] = { "Usage:\n" \
	"$ %s [<options_list>]\n\n" \
	"\t/CONFIG=<file>     configuration options file path\n" \
	"\t/TRACE             enable extensible diagnostic output\n" \
	"\t/LOGFILE=<file>    a specification of file to accept logging\n" \
	"\t/LOGSIZE=<number>  a maximum size of file in octets\n" \
	"\t/SETTINGS=<file>   a run-time configuration for the network stuff and the serial devices\n" \

	"\n\tExample of usage:\n\t $ %s -config=n2s_config.conf /settings=n2s_settings.conf /trace\n" };


/*
 *   DESCRIPTION: Lookup a serial context for a given device in the global table.
 *
 *   INPUTS:
 *	a_target:	A device name for looking for
 *
 *   OUTPUTS:
 *	a_serial:	A returned serial context
 *
 *   RETURNS:
 *	condition code
 */
static int s_settings_find_n_get_serial (
			const	char	*a_target,
			N2S$_SERIAL	**a_serial
		)
{
	/*
	 * Run over has been defined array of serials - lookup by give name.
	 * The comparison is case SENSITIVE: the device paths of the UNIX systems are.
	 */
	for ( int i = 0; i < g_serials_nr; i++)
		{
		if ( !strncmp(a_target, g_serials[i].devname, N2S$K_TTY_DEVNAME) )
			return	*a_serial = &g_serials[i], STS$K_SUCCESS;
		}

	return	$LOG(STS$K_ERROR, "Target device <%s> has not been defined", a_target);
}


/*
 *   DESCRIPTION: Translate a flow control mnemonic from the settings file to the internal
 *	representative.
 *
 *   INPUTS:
 *	a_str:		A mnemonic: "NONE", "XON/XOFF", "RTS/CTS"
 *
 *   OUTPUTS:
 *	a_flow:		A flow control discipline, see the N2S$K_FLOW_* constants
 *
 *   RETURNS:
 *	condition code
 */
static int s_settings_str2flow (
			const char	*a_str,
				int	*a_flow
		)
{
	if ( !strcasecmp(a_str, "NONE") )
		return	*a_flow = N2S$K_FLOW_NONE, STS$K_SUCCESS;

	if ( (!strcasecmp(a_str, "XON/XOFF")) || (!strcasecmp(a_str, "XONXOFF")) )
		return	*a_flow = N2S$K_FLOW_XONXOFF, STS$K_SUCCESS;

	if ( (!strcasecmp(a_str, "RTS/CTS")) || (!strcasecmp(a_str, "RTSCTS")) )
		return	*a_flow = N2S$K_FLOW_RTSCTS, STS$K_SUCCESS;

	*a_flow = N2S$K_FLOW_NONE;

	return	STS$K_ERROR;
}




/*
 *  DESCRIPTION: Process settings for serial communication devices with validation, fill global
 *	table of serial devices by new records. An illformed record is rejected with a diagnostic,
 *	the processing is continued from the next one.
 *
 *  INPUTS:
 *	a_cfg:		A context of LIBCONFIG for settings file
 *
 *  OUTPUTS:
 *	NONE
 *
 *  IMPLICITE OUTPUTS:
 *	g_serials, g_serials_nr
 *
 *  RETURNS:
 *	condition code
 *
 */
static int	s_settings_process_serials (config_t	*a_cfg)
{
config_setting_t *l_setting, *l_args;
const char *l_str;
int	l_count, l_int = -1, l_speed, l_databits, l_stopbits;
char	l_parity = 0;
N2S$_SERIAL	*l_serial;
char	l_chars[64];

	if ( !(l_setting = config_lookup(a_cfg, "serials")) )
		return	$LOG(STS$K_ERROR, "No <serials> section --- %s", config_error_text(a_cfg));

	l_serial = g_serials;

	l_count = config_setting_length(l_setting);

	if ( l_count > N2S$K_MAX_SERIALS )				/* Never run out of the g_serials[] capacity	*/
		{
		$LOG(STS$K_WARN, "Only %d of %d <serials> records will be processed", N2S$K_MAX_SERIALS, l_count);
		l_count = N2S$K_MAX_SERIALS;
		}


	for (int i = 0; i < l_count; i++)
		{
		memset(l_serial, 0, sizeof(N2S$_SERIAL) );

		if ( !(l_args = config_setting_get_elem(l_setting, i)) )
			{
			$LOG(STS$K_ERROR, "[serial #%02d] --- illformed or missing", i);
			continue;
			}

		if ( !config_setting_lookup_string(l_args, "device", &l_str) )
			{
			$LOG(STS$K_ERROR, "[serial #%02d] --- no <device> option", i);
			continue;
			}

		strncpy(l_serial->devname, l_str, N2S$K_TTY_DEVNAME);	/* The tail is zeroed by the memset above	*/

		if ( config_setting_lookup_string(l_args, "desc", &l_str) )
			strncpy(l_serial->desc, l_str, N2S$K_TTY_DESC);

		/*
		 * config_setting_lookup_int() reports a PRESENCE of the key, so the value must be
		 * checked separately - otherwise "rs485 = 0;" would enable the RS485 support.
		 */
		if ( config_setting_lookup_int(l_args, "rs485", &l_int) && l_int )
			{
#ifdef HAVE_TIOCRS485
			l_serial->flags |=  N2S$M_SERIAL_RS485;
#else
			$LOG(STS$K_WARN, "[serial #%02d:<%s>] --- RS485 option is not supported", i, l_serial->devname);
#endif
			}

		if ( !config_setting_lookup_string(l_args, "chars", &l_str) )
			{
			$LOG(STS$K_ERROR, "[serial #%02d:<%s>] --- no <chars> option", i, l_serial->devname);
			continue;
			}

		/*
		 * The string is collapsed in a local copy: libconfig owns the original one and we
		 * must not modify it in place.
		 */
		if ( sizeof(l_chars) <= (size_t) snprintf(l_chars, sizeof(l_chars), "%s", l_str) )
			{
			$LOG(STS$K_ERROR, "[serial #%02d:<%s>] --- <chars> value is too long", i, l_serial->devname);
			continue;
			}

		__util$collapse(l_chars, strlen(l_chars));		/* "115200, 8, N, 1" -> "115200,8,N,1"		*/

		if ( 4 != sscanf(l_chars, "%d,%d,%c,%d", &l_speed, &l_databits, &l_parity, &l_stopbits) )
			{
			$LOG(STS$K_ERROR, "[serial #%02d:<%s>] --- <chars> value <%s> is illformed", i,
				l_serial->devname, l_chars);
			continue;
			}

		/*
		 * Validate the line parameters: an out of range value is rejected with the allowed
		 * range in the message, so the operator does not have to guess.
		 */
		if ( !$ISINRANGE(l_speed, N2S$K_BAUD_MIN, N2S$K_BAUD_MAX) )
			{
			$LOG(STS$K_ERROR, "[serial #%02d:<%s>] --- speed %d baud is out of range [%d..%d]", i,
				l_serial->devname, l_speed, N2S$K_BAUD_MIN, N2S$K_BAUD_MAX);
			continue;
			}

		if ( !$ISINRANGE(l_databits, N2S$K_DATABITS_MIN, N2S$K_DATABITS_MAX) )
			{
			$LOG(STS$K_ERROR, "[serial #%02d:<%s>] --- %d data bits is out of range [%d..%d]", i,
				l_serial->devname, l_databits, N2S$K_DATABITS_MIN, N2S$K_DATABITS_MAX);
			continue;
			}

		if ( !$ISINRANGE(l_stopbits, N2S$K_STOPBITS_MIN, N2S$K_STOPBITS_MAX) )
			{
			$LOG(STS$K_ERROR, "[serial #%02d:<%s>] --- %d stop bits is out of range [%d..%d]", i,
				l_serial->devname, l_stopbits, N2S$K_STOPBITS_MIN, N2S$K_STOPBITS_MAX);
			continue;
			}

		switch (l_parity = toupper (l_parity))
			{
			case	'N':
			case	'E':
			case	'O':
				break;

			default:
				$LOG(STS$K_ERROR, "[serial #%02d:<%s>] --- parity '%c' is unknown, allowed: [N, E, O]", i,
					l_serial->devname, l_parity);
				continue;
			}

		/*
		 * Form context for serial device
		 */
		l_serial->fd = -1;
		l_serial->owner_sd = -1;
		l_serial->baud = l_speed;
		l_serial->parity = l_parity;
		l_serial->stopbits = l_stopbits;
		l_serial->databits = l_databits;
		l_serial->flow = N2S$K_FLOW_NONE;			/* A default which can be overriden below	*/
		l_serial->iotmo = N2S$K_NET_TMO_MSEC;

		if ( config_setting_lookup_string(l_args, "flow", &l_str) )
			{
			if ( !(1 & s_settings_str2flow (l_str, &l_serial->flow)) )
				{
				$LOG(STS$K_ERROR, "[serial #%02d:<%s>] --- flow control <%s> is unknown, "
					"allowed: [NONE, XON/XOFF, RTS/CTS]", i, l_serial->devname, l_str);
				continue;
				}
			}

		if ( config_setting_lookup_int(l_args, "iotmo", &l_int) )
			{
			if ( !$ISINRANGE(l_int, N2S$K_IOTMO_MIN, N2S$K_IOTMO_MAX) )
				{
				$LOG(STS$K_ERROR, "[serial #%02d:<%s>] --- iotmo %d msec is out of range [%d..%d]", i,
					l_serial->devname, l_int, N2S$K_IOTMO_MIN, N2S$K_IOTMO_MAX);
				continue;
				}

			l_serial->iotmo = l_int;
			}

		if ( (l_int = pthread_mutex_init(&l_serial->lock, NULL)) )
			{
			$LOG(STS$K_ERROR, "[serial #%02d:<%s>] --- pthread_mutex_init()->%d, errno: %d", i,
				l_serial->devname, l_int, errno);
			continue;
			}

		$LOG(STS$K_INFO, "Added device #%02d [<%s>, Chars: <%d, %d, %c, %d>, Flow: <%s>, I/O Tmo: %d msec] --- added", i,
			l_serial->devname, l_serial->baud, l_serial->databits, l_serial->parity, l_serial->stopbits,
			(l_serial->flow == N2S$K_FLOW_NONE) ? "NONE" : ((l_serial->flow == N2S$K_FLOW_XONXOFF) ? "XON/XOFF" : "RTS/CTS"),
			l_serial->iotmo);


		l_serial++;
		g_serials_nr++;
		}

	return	g_serials_nr ? STS$K_SUCCESS : $LOG(STS$K_ERROR, "No serials has been defined!");
}




/*
 *  DESCRIPTION: Process settings for network listeners with validation, fill global table of
 *	listeners by new records. An illformed record is rejected with a diagnostic, the
 *	processing is continued from the next one.
 *
 *  INPUTS:
 *	a_cfg:		A context of LIBCONFIG for settings file
 *
 *  OUTPUTS:
 *	NONE
 *
 *  IMPLICITE OUTPUTS:
 *	g_listeners, g_listeners_nr
 *
 *  RETURNS:
 *	condition code
 *
 */
static int	s_settings_process_listeners (config_t	*a_cfg)
{
config_setting_t *l_setting, *l_args;
const char *l_str;
int	l_count, l_int = -1, l_port_nr;
N2S$_LISTENER	*l_listener;
char	l_bind[128], l_proto[16], l_laddr[32], l_port[8];

	if ( !(l_setting = config_lookup(a_cfg, "listeners")) )
		return	$LOG(STS$K_ERROR, "No <listeners> section --- %s", config_error_text(a_cfg));

	l_listener = g_listeners;

	l_count = config_setting_length(l_setting);

	if ( l_count > N2S$K_MAX_LISTENERS )				/* Never run out of the g_listeners[] capacity	*/
		{
		$LOG(STS$K_WARN, "Only %d of %d <listeners> records will be processed", N2S$K_MAX_LISTENERS, l_count);
		l_count = N2S$K_MAX_LISTENERS;
		}


	for (int i = 0; i < l_count; i++)
		{
		memset(l_listener, 0, sizeof(N2S$_LISTENER));		/* Never reuse a leftover of a rejected record	*/

		if ( !(l_args = config_setting_get_elem(l_setting, i)) )
			{
			$LOG(STS$K_ERROR, "[listener #%02d] --- illformed or missing", i);
			continue;
			}

		if ( !config_setting_lookup_string(l_args, "bind", &l_str) )
			{
			$LOG(STS$K_ERROR, "[listener #%02d] --- no <bind> option", i);
			continue;
			}

		/*
		 * The string is collapsed in a local copy: libconfig owns the original one and we
		 * must not modify it in place.
		 */
		if ( sizeof(l_bind) <= (size_t) snprintf(l_bind, sizeof(l_bind), "%s", l_str) )
			{
			$LOG(STS$K_ERROR, "[listener #%02d] --- <bind> value is too long", i);
			continue;
			}

		__util$collapse(l_bind, strlen(l_bind));		/* "TCP: 0.0.0.0: 502" -> "TCP:0.0.0.0:502"	*/

		if ( 3 != sscanf(l_bind, "%15[TCPUDPtcpudp]:%31[0-9.]:%7[0-9]", l_proto, l_laddr, l_port) )
			{
			$LOG(STS$K_ERROR, "[listener #%02d] --- <bind> value <%s> is illformed", i, l_bind);
			continue;
			}

		switch ( toupper (*l_proto) )
			{
			case	'T':
				l_listener->proto = IPPROTO_TCP;
				break;

			case	'U':					/* listen()/accept() on SOCK_DGRAM does not work */
				$LOG(STS$K_ERROR, "[listener #%02d] --- UDP transport is not implemented yet", i);
				continue;

			default:
				$LOG(STS$K_ERROR, "[listener #%02d] --- <bind> value <%s> is illformed", i, l_bind);
				continue;
			}

		if ( !$ISINRANGE((l_port_nr = atoi(l_port)), N2S$K_PORT_MIN, N2S$K_PORT_MAX) )
			{
			$LOG(STS$K_ERROR, "[listener #%02d] --- port number %d is out of range [%d..%d]", i,
				l_port_nr, N2S$K_PORT_MIN, N2S$K_PORT_MAX);
			continue;
			}

		l_listener->sk.sin_family = AF_INET;
		l_listener->sk.sin_port = htons ((uint16_t) l_port_nr);

		if ( 1 != (l_int = inet_pton(AF_INET, l_laddr, &l_listener->sk.sin_addr)) )
			{
			$LOG(STS$K_ERROR, "[listener #%02d] --- inet_pton(<%s>)->%d, errno: %d", i, l_laddr, l_int, errno);
			continue;
			}

		l_listener->iotmo = N2S$K_NET_TMO_MSEC;			/* The defaults which can be overriden below	*/
		l_listener->connlm = N2S$K_CONNLM_MIN;

		if ( config_setting_lookup_int(l_args, "iotmo", &l_int) )
			{
			if ( !$ISINRANGE(l_int, N2S$K_IOTMO_MIN, N2S$K_IOTMO_MAX) )
				{
				$LOG(STS$K_ERROR, "[listener #%02d] --- iotmo %d msec is out of range [%d..%d]", i,
					l_int, N2S$K_IOTMO_MIN, N2S$K_IOTMO_MAX);
				continue;
				}

			l_listener->iotmo = l_int;
			}

		if ( config_setting_lookup_int(l_args, "connlm", &l_int) )
			{
			if ( !$ISINRANGE(l_int, N2S$K_CONNLM_MIN, N2S$K_CONNLM_MAX) )
				{
				$LOG(STS$K_ERROR, "[listener #%02d] --- connlm %d is out of range [%d..%d]", i,
					l_int, N2S$K_CONNLM_MIN, N2S$K_CONNLM_MAX);
				continue;
				}

			l_listener->connlm = l_int;
			}

		if ( !config_setting_lookup_string(l_args, "target", &l_str) )
			{
			$LOG(STS$K_ERROR, "[listener #%02d] --- no <target> device", i);
			continue;
			}

		if ( !(1 & s_settings_find_n_get_serial (l_str, &l_listener->serial)) )
			{
			$LOG(STS$K_ERROR, "[listener #%02d] --- target <%s> has not been define in <serial> section", i, l_str);
			continue;
			}

		strncpy(l_listener->target, l_str, N2S$K_TTY_DEVNAME);	/* The tail is zeroed by the memset above	*/


		$LOG(STS$K_INFO, "Added listener #%02d [Target: <%s>, Net: <%s:" IPv4_BYTES_FMT ":%d>, I/O Tmo: %d msec, Backlog: %d] --- added", i,
			l_listener->serial->devname,
			"TCP", IPv4_BYTES(l_listener->sk.sin_addr.s_addr), ntohs(l_listener->sk.sin_port),
			l_listener->iotmo, l_listener->connlm);


		l_listener++;
		g_listeners_nr++;
		}


	return	g_listeners_nr ? STS$K_SUCCESS : $LOG(STS$K_ERROR, "No listeners has been defined!");
}




/*
 *   DESCRIPTION: Load the settings file by the LIBCONFIG API and process its sections: the
 *	<serials> table first (the listeners refer to the serial devices), then the <listeners>
 *	table. A failure of any stage aborts the whole processing.
 *
 *   INPUTS:
 *	a_settings_conf: A file specification of the settings file
 *
 *   OUTPUTS:
 *	NONE
 *
 *   IMPLICITE OUTPUTS:
 *	g_cfg, g_serials, g_serials_nr, g_listeners, g_listeners_nr
 *
 *   RETURNS:
 *	condition code
 */
static int	s_settings_load_n_parse (
		const char	*a_settings_conf
		)
{
int	l_rc;

	config_init(&g_cfg);
#ifdef	CONFIG_OPTION_IGNORECASE
	config_set_options (&g_cfg, CONFIG_OPTION_IGNORECASE);
#endif
	if ( !$ASCLEN(&g_settings) )
		return	$LOG(STS$K_ERROR, "Settings file must be provided with the /SETTINGS=<fspec> option");

	/* Read the file. If there is an error, report it and exit. */
	if( !config_read_file(&g_cfg, a_settings_conf))
		{
		$LOG(STS$K_ERROR, "%s:%d - %s, errno: %d", config_error_file(&g_cfg), config_error_line(&g_cfg),
			config_error_text(&g_cfg), errno);
		config_destroy(&g_cfg);
		return	STS$K_ERROR;
		}

	if ( !(1 & (l_rc = s_settings_process_serials (&g_cfg))) )
		return	l_rc;

	return	s_settings_process_listeners (&g_cfg);
}



/*
 *   DESCRIPTION: Validate the has been accepted run-time parameters as a whole.
 *
 *   INPUTS:
 *	NONE
 *
 *   OUTPUTS:
 *	NONE
 *
 *   IMPLICITE INPUTS:
 *	g_settings
 *
 *   RETURNS:
 *	condition code
 */
static int	s_config_validation	(void)
{
	return	s_settings_load_n_parse ($ASCPTR(&g_settings));
}



/*
 *   DESCRIPTION: A handler of the asynchronous signals. The termination signals (SIGTERM,
 *	SIGINT, SIGQUIT) do set the <g_exit_flag>, a repeated one terminates the process
 *	immediately. SIGUSR1 toggles the tracing at run-time. Only the async-signal-safe calls
 *	are used here: write() instead of fprintf()/fflush().
 *
 *   INPUTS:
 *	a_signo:	A number of the has been delivered signal
 *
 *   OUTPUTS:
 *	NONE
 *
 *   IMPLICITE OUTPUTS:
 *	g_exit_flag, g_trace
 *
 *   RETURNS:
 *	NONE
 */
static	void	s_sig_handler (int a_signo)
{
	if ( g_exit_flag )
		{
		static const char l_msg [] = "Exit flag has been set, exiting ...\n";
		if ( 0 > write(STDOUT_FILENO, l_msg, sizeof(l_msg) - 1) ) {;}

		_exit(a_signo);
		}


	if ( (a_signo == SIGTERM) || (a_signo == SIGINT) || (a_signo == SIGQUIT))
		{
		static const char l_msg [] = "Got a termination signal, set exit_flag!\n";
		if ( 0 > write(STDOUT_FILENO, l_msg, sizeof(l_msg) - 1) ) {;}

		g_exit_flag = 1;
		return;
		}
	else if ( a_signo == SIGUSR1)
		{
		g_trace = !g_trace;

		if ( g_trace )
			{
			static const char l_msg [] = "Set /TRACE=ON\n";
			if ( 0 > write(STDOUT_FILENO, l_msg, sizeof(l_msg) - 1) ) {;}
			}
		else	{
			static const char l_msg [] = "Set /TRACE=OFF\n";
			if ( 0 > write(STDOUT_FILENO, l_msg, sizeof(l_msg) - 1) ) {;}
			}

		return;
		}
	else	{
		static const char l_msg [] = "Got an unexpected signal\n";
		if ( 0 > write(STDOUT_FILENO, l_msg, sizeof(l_msg) - 1) ) {;}
		}

	_exit(a_signo);
}


/*
 *   DESCRIPTION: Establish the handler of the asynchronous signals for the termination and the
 *	trace toggling, ignore SIGPIPE (a write to a has been closed socket must be seen as an
 *	EPIPE error, not as a process termination).
 *
 *   INPUTS:
 *	NONE
 *
 *   OUTPUTS:
 *	NONE
 *
 *   RETURNS:
 *	NONE
 */
static void	s_init_sig_handler(void)
{
const int siglist [] = {SIGTERM, SIGINT, SIGUSR1, SIGQUIT, 0 };

	/*
	 * Establishing a signals handler
	 */
	signal(SIGPIPE, SIG_IGN);	/* We don't want to crash the server due fucking unix shit */

	for ( int i = 0; siglist[i]; i++)
		{
		if ( (signal(siglist[i], s_sig_handler)) == SIG_ERR )
			$LOG(STS$K_ERROR, "Error establishing handler for signal %d/%#x, errno: %d",
				siglist[i], siglist[i], errno);

		$IFTRACE(g_trace, "Set handler for signal %d/%#x (%s)", siglist[i], siglist[i], strsignal(siglist[i]));
		}
}



/*
 *   DESCRIPTION: Main entry point of the program: accept and parse the command line arguments,
 *	load and validate the run-time parameters, establish the signal handling, start the
 *	network subsystem and stay in the main loop until the exit flag is set, then stop the
 *	subsystems in the reverse order.
 *
 *   INPUTS:
 *	argc:		A count of the command line arguments
 *	argv:		An array of the command line arguments; see g_optstbl for the accepted
 *			options (/CONFIG, /TRACE, /LOGFILE, /LOGSIZE, /SETTINGS)
 *
 *   OUTPUTS:
 *	NONE
 *
 *   RETURNS:
 *	EXIT_SUCCESS - a normal termination by the exit flag, EXIT_FAILURE - otherwise
 */
int	main(int argc, char **argv)
{
int l_rc = 0;


	__util$inimsg(&__n2s_msgs_desc__);

	$PUTMSG_FAO(N2S$__REVISNF, &__ident__, &__rev__);

	/*
	 * Process command line arguments
	 */
	if ( !(1 & __util$getparams(argc, argv, g_optstbl)) )
		return	$LOG(STS$K_ERROR, "Error processing configuration"), EXIT_FAILURE;


	if ( $ASCLEN(&g_logfspec) )
		{
		__util$deflog($ASCPTR(&g_logfspec), NULL);

		$PUTMSG_FAO(N2S$__REVISNF, &__ident__, &__rev__);
		}

	if ( g_trace )
		__util$showparams(g_optstbl);

	if ( !(1 & s_config_validation()) )
		return	$LOG(STS$K_FATAL, "Abort execution, check configuration!!!"), EXIT_FAILURE;



	s_init_sig_handler ();

	if ( (1 & (l_rc = n2s$net_start_listeners ())) )
		{
		while ( !g_exit_flag)
			{
			for (int delay = 3;  (delay = sleep(delay)); );
			}
		l_rc = n2s$net_stop_listeners ();
		}

	$PUTMSG_FAO(N2S$__EXITST, g_exit_flag, l_rc);

	return	(1 & l_rc) ? EXIT_SUCCESS : EXIT_FAILURE;
}
