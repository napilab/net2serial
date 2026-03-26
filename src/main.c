#define	__MODULE__	"N2S-MAIN"
#define	__IDENT__	"X.00-04"
#define	__REV__		"00.00.04"

/*
**++
**
**  FACILITY: A yet another Network to Serial gateway
**
**  ENVIRONMENT: Linux
**
**  ABSTRACT: A server to transparently passing data has been received over TCP-connection to a local serial port.
**
**  DESCRIPTION: Main routine, read and process connfiguration, start workers.
**
**  AUTHORS: Ruslan R. (The BadAss Sysman) Laishev
**
**  CREATION DATE:  11-FEB-2026
**
**
**  USAGE:
**	$ tootoo3_cgw [options]
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
**--
*/

#include	<signal.h>
#include	<stdint.h>
#include	<time.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
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


static const	ASC	s__ident__ = {$ASCINI(__FAC__ " "  __IDENT__ "/"  __ARCH__NAME__   "(built at "__DATE__ " " __TIME__ " with CC " __VERSION__ ")")},
	s__rev__ = {$ASCINI(__REV__)};



static	EMSG_RECORD __n2s_msgs__ [] = {					/* Create array of message records */
	#define	$DEF_MSG(f, s, c, t)	{ f##$__##c, 0, {"%%" #f "-"    STS$C__##s     "-" #c ", " t } },
	__DEF_MESSAGES__
	#undef	$DEF_MSG
};


static	EMSG_RECORD_DESC __n2s_msgs_desc__  = {
	.link = NULL,							/* Must beee NULL here */
	.facno = N2S,
	.msgnr = sizeof(__n2s_msgs__) / sizeof(__n2s_msgs__[0]),
	.msgrec = __n2s_msgs__					/* An address of the message records array */
};


ASC	g_logfspec = {0},
	g_confspec = {0},
	q_settings = {0}
	;


int	g_exit_flag = 0,						/* Global flag 'all must to be stop'	*/
	g_trace = 0;							/* A flag to produce extensible logging	*/

int	g_logsize = 0							/* A size in octets of the log file */
	;



N2S$_SERIAL	g_serials[N2S$K_MAX_SERIALS];				/* A table of serials, filled from configuration at startup time */
int		g_serials_nr;						/* Count of records in the table */

N2S$_LISTENER	g_listeners[N2S$K_MAX_LISTENERS];			/* A table of serials, filled from configuration at startup time */
int		g_listeners_nr;						/* Count of records in the table */



config_t	g_cfg;							/* Lib Config API context */



static const OPTS g_optstbl [] =					/* General CLI options		*/
{
	{$ASCINI("config"),	&g_confspec, ASC$K_SZ,		OPTS$K_CONF},
	{$ASCINI("trace"),	&g_trace, 0,			OPTS$K_OPT},
	{$ASCINI("logfile"),	&g_logfspec, ASC$K_SZ,		OPTS$K_STR},
	{$ASCINI("logsize"),	&g_logsize, 0,			OPTS$K_INT},

	{$ASCINI("settings"),	&q_settings,  ASC$K_SZ,		OPTS$K_STR},

	OPTS_NULL
};



const char	help [] = { "Usage:\n" \
	"$ %s [<options_list>]\n\n" \
	"\t/CONFIG=<file>     configuration options file path\n" \
	"\t/TRACE             enable extensible diagnostic output\n" \
	"\t/LOGFILE=<file>    a specification of file to accept logging\n" \
	"\t/LOGSIZE=<number>  a maximum size of file in octets\n" \

	"\n\tExample of usage:\n\t $ %s -config=n2s_config.conf /settings=n2s_settings.conf /trace\n" };


/*
 *   DESCRIPTION: Lookup a serial context for a given device in the global table
 *
 *   INPUTS:
 *	a_target:	A device name for looking for
 *
 *   OUTPUTS:
 *	a_serial:	A returned serial context
 *
 *   RETURNS:
 *	confdition code
 */
static int s_settings_find_n_get_serial (
			const	char	*a_target,
			N2S$_SERIAL	**a_serial
		)
{
	/*
	 * Run over has been defined array of serials - lookup by give name
	 */
	for ( int i = 0; i < g_serials_nr; i++)
		{
		if ( !strncasecmp(a_target, g_serials[i].devname, N2S$K_TTY_DEVNAME) )
			return	*a_serial = &g_serials[i], STS$K_SUCCESS;
		}

	return	$LOG(STS$K_ERROR, "Target device <%s> has not been defined", a_target);
}




/*
 *  DESCRIPTION: Process settings for serial communication devices with validation,
 *	fill global table of serial devices by new records.
 *
 *  INPUTS:
 *	a_cfg:		A context of LIBCONFIG for settings file
 *
 *  OUTPUTS:
 *	NONE
 *
 *  RETURNS:
 *	condition code
 *
 */
int	s_settings_process_serials (config_t	*a_cfg)
{
config_setting_t *l_setting, *l_args;
const char *l_str;
int	l_count, l_int = -1, l_speed, l_databits, l_stopbits, l_parity = 0;
N2S$_SERIAL	*l_serial;

	if ( !(l_setting = config_lookup(a_cfg, "serials")) )
		return	$LOG(STS$K_ERROR, "No <serials> section --- %s", config_error_text(a_cfg));

	l_serial = g_serials;

	l_count = config_setting_length(l_setting);


	for (int i = 0; i < l_count; i++)
		{
		memset(l_serial, 0, sizeof(N2S$_SERIAL) );


		if ( !(l_args = config_setting_get_elem(l_setting, i)) )
			{
			$LOG(STS$K_ERROR, "[serial #%02d] --- illformed or missing", i);
			continue;
			}



		if ( config_setting_lookup_string(l_args, "device", &l_str) )
			strncpy(l_serial->devname, l_str, N2S$K_TTY_DEVNAME);


#ifdef HAVE_TIOCRS485
		if ( config_setting_lookup_int(l_args, "rs485", &l_int) )
			l_serial->flags |=  (l_int) ?  N2S$M_SERIAL_RS485 : 0;

#else
		if ( config_setting_lookup_int(l_args, "rs485", &l_int) )
			$LOG(STS$K_WARN, "[serial #%02d] --- RS485 option is not supported", i, l_serial->devname );

#endif


		if ( config_setting_lookup_string(l_args, "chars", &l_str) )
			{
			__util$collapse((char *) l_str, l_int = strlen((char *) l_str));			/* "115200, 8, N, 1" -> "115200,8,N,1" */

			if ( 4 != sscanf(l_str, "%d,%d,%c,%d", &l_speed, &l_databits, &l_parity, &l_stopbits) )
				{
				$LOG(STS$K_ERROR, "[serial #%02d] --- chars <%s> illformed", i, l_serial->devname );
				continue;
				}

			l_serial->baud = l_speed;

			switch (l_parity = toupper (l_parity))
				{
				case	'N':
				case	'E':
				case	'O':
					break;

				default:
					$LOG(STS$K_ERROR, "[serial #%02d] --- chars <%s> illformed", i, l_str);
					continue;
				}

			/*
			 * From context for serial device
			 */
			l_serial->parity = l_parity;
			l_serial->stopbits = l_stopbits;
			l_serial->databits = l_databits;
			l_serial->fd = -1;

			pthread_mutex_init(&l_serial->lock, 0);
			}

		$LOG(STS$K_INFO, "Added device #%02d [<%s>, Chars: <%d, %d, %c, %d>] --- added", i,
			&l_serial->devname, l_serial->baud, l_serial->databits, l_serial->parity, l_serial->stopbits);


		l_serial++;
		g_serials_nr++;
		}

	return	g_serials_nr ? STS$K_SUCCESS : $LOG(STS$K_ERROR, "No serials has been defined!");
}




int	s_settings_process_listeners (config_t	*a_cfg)
{
config_setting_t *l_setting, *l_args;
const char *l_str;
int	l_count, l_int = -1;
N2S$_LISTENER	*l_listener;
char	l_proto[32], l_laddr[32], l_port[32];

	if ( !(l_setting = config_lookup(a_cfg, "listeners")) )
		return	$LOG(STS$K_ERROR, "No <listeners> section --- %s", config_error_text(a_cfg));

	l_listener = g_listeners;

	l_count = config_setting_length(l_setting);


	for (int i = 0; i < l_count; i++)
		{
		if ( !(l_args = config_setting_get_elem(l_setting, i)) )
			{
			$LOG(STS$K_ERROR, "[listener #%02d] --- illformed or missing", i);
			continue;
			}


		if ( config_setting_lookup_string(l_args, "bind", &l_str) )
			{
			__util$collapse((char *) l_str, l_int = strlen( (char *) l_str));			/* "TCP: 0.0.0.0: 502" -> "TCP:0.0.0.0:502" */

			if ( 3 != (l_int = sscanf(l_str, "%[TCPUDP]:%[0-9.]:%[0-9]", l_proto, l_laddr, l_port)) )
				{
				$LOG(STS$K_ERROR, "[serial #%02d] --- chars <%s> illformed", i, l_str);
				continue;
				}

			l_listener->sk.sin_family = AF_INET;
			l_listener->sk.sin_port = htons (atoi(l_port));
			inet_pton(AF_INET, l_laddr, &l_listener->sk.sin_addr);


			switch ( toupper (*l_proto) )
				{
				case	'T':	l_listener->proto = IPPROTO_TCP; break;
				case	'U':	l_listener->proto = IPPROTO_UDP; break;

				default:
					$LOG(STS$K_ERROR, "[listener #%02d] --- chars <%s> illformed", i, l_str);
					continue;
				}

			if ( config_setting_lookup_int(l_args, "iotmo", &l_int) )
				l_listener->iotmo = l_int;
			if ( config_setting_lookup_int(l_args, "connlm", &l_int) )
				l_listener->connlm = l_int;

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


			__util$collapse((char *) l_str, strlen(l_str));
			__util$str2asc(l_str, l_listener->target);
			}


		$LOG(STS$K_INFO, "Added listener #%02d [Target: <%s>, Net: <%s:" IPv4_BYTES_FMT ":%d>, I/O Tmo: %d msec, Connection limit: %d] --- added", i,
			l_listener->serial->devname,
			(l_listener->proto == IPPROTO_UDP) ? "UDP": "TCP", IPv4_BYTES(l_listener->sk.sin_addr.s_addr), ntohs(l_listener->sk.sin_port),
			l_listener->iotmo, l_listener->connlm);


		l_listener++;
		g_listeners_nr++;
		}


	return	g_listeners_nr ? STS$K_SUCCESS : $LOG(STS$K_ERROR, "No listeners has been defined!");
}





static int	s_settings_load_n_parse (
		const char	*a_settings_conf
		)
{
const char *l_str;
int	l_rc;

	config_init(&g_cfg);
#ifdef	CONFIG_OPTION_IGNORECASE
	config_set_options (&g_cfg, CONFIG_OPTION_IGNORECASE);
#endif
	if ( !$ASCLEN(&q_settings) )
		return	$LOG(STS$K_ERROR, "Settings file must be provided with the /SETTINGS=<fspec> option");

	/* Read the file. If there is an error, report it and exit. */
	if( !config_read_file(&g_cfg, a_settings_conf))
		{
		$LOG(STS$K_ERROR, "%s:%d - %s, errno: %d", config_error_file(&g_cfg), config_error_line(&g_cfg), config_error_text(&g_cfg), errno);
		config_destroy(&g_cfg);
		return	STS$K_ERROR;
		}

	l_rc = s_settings_process_serials (&g_cfg);
	l_rc = s_settings_process_listeners (&g_cfg);

	return	l_rc;
}




static int	s_config_validation	(void)
{
int l_rc;

	l_rc = s_settings_load_n_parse ($ASCPTR(&q_settings));
	return	l_rc;
}



static	void	s_sig_handler (int signo)
{
	if ( g_exit_flag )
		{
		fprintf(stdout, "Exit flag has been set, exiting ...\n");
		fflush(stdout);

		_exit(signo);
		}


	if ( (signo == SIGTERM) || (signo == SIGINT) || (signo == SIGQUIT))
		{
		fprintf(stdout, "Get the %d/%#x, set exit_flag!\n", signo, signo);
		fflush(stdout);

		g_exit_flag = 1;
		return;
		}
	else if ( signo == SIGUSR1)
		{
		g_trace = !g_trace;
		fprintf(stdout, "Set /TRACE=%s\n", g_trace ? "ON" : "OFF");
		fflush(stdout);
		return;
		}
	else	{
		fprintf(stdout, "Get the %d/%#x signal\n", signo, signo);
		fflush(stdout);
		}

	_exit(signo);
}

static void	s_init_sig_handler(void)
{
const int siglist [] = {SIGTERM, SIGINT, SIGUSR1, SIGQUIT, 0 };
int	i;

	/*
	 * Establishing a signals handler
	 */
	signal(SIGPIPE, SIG_IGN);	/* We don't want to crash the server due fucking unix shit */

	for ( i = 0; siglist[i]; i++)
		{
		if ( (signal(siglist[i], s_sig_handler)) == SIG_ERR )
			$LOG(STS$K_ERROR, "Error establishing handler for signal %d/%#x, error=%d", siglist[i], siglist[i], errno);

		$IFTRACE(g_trace, "Set handler for signal %d/%#x (%s)", siglist[i], siglist[i], strsignal(siglist[i]));
		}
}



/*
 *   DESCRIPTION: Main entry point for the program. Accept command line arguments, parse, validate run-time parameters,
 *	start execution threads.
 *
 *   INPPUTS:
 *	argc:
 *	argv:
 *
 *   OUTPUTS: NONE
 *
 *   RETURNS:
 *	{tbs}
 */

int	main(int argc, char **argv)
{
int l_rc = 0;


	__util$inimsg(&__n2s_msgs_desc__);

	$LOG(STS$K_INFO, "Rev: " __IDENT__ "/"  __ARCH__NAME__   ", (built  at "__DATE__ " " __TIME__ " with CC " __VERSION__ ")");

	/*
	 * Process command line arguments
	 */
	if ( !(1 & __util$getparams(argc, argv, g_optstbl)) )
		return	$LOG(STS$K_ERROR, "Error processing configuration");


	if ( $ASCLEN(&g_logfspec) )
		{
		__util$deflog($ASCPTR(&g_logfspec), NULL);

		$LOG(STS$K_INFO, "Rev: " __IDENT__ "/"  __ARCH__NAME__   ", (built  at "__DATE__ " " __TIME__ " with CC " __VERSION__ ")");
		}

	if ( g_trace )
		__util$showparams(g_optstbl);

	if ( !(1 & s_config_validation()) )
		return	$LOG(STS$K_FATAL, "Abort execution, check configuration!!!");



	s_init_sig_handler ();

	if ( (1 & (l_rc = n2s$net_start_listeners ())) )
		{
		while ( !g_exit_flag)
			{
			for (int delay = 3;  delay = sleep(delay); );
			}
		l_rc = n2s$net_stop_listeners ();
		}

	return	$LOG(STS$K_INFO, "Exit with exit_flag=%d, rc=%d", g_exit_flag, l_rc);
}
