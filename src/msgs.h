
#ifndef	__N2S_MSGS__
#define __N2S_MSGS__	1

#include	"utility_routines.h"		/* STS$K-* constants */
#include	"defs.h"			/* RTE's formats for IP and MAC addresses */

#ifdef __cplusplus
extern "C" {
#endif

#define	N2S		139			/* Facility number ! This number is should be unique */

/*	Attention !!! Place new messages at end of this macro !!! */
/*	<facility>,<severity>, <abbreviation>, <message text with format specifiers> */

#define __DEF_MESSAGES__ \
\
\
$DEF_MSG(N2S, SUCCESS,	NETLSNR,		"Start network listener [%s, " IPv4_BYTES_FMT ":%d]")\
$DEF_MSG(N2S, SUCCESS,	NETCONN,		"Accept client connection request  [%s, " IPv4_BYTES_FMT ":%d ->" IPv4_BYTES_FMT ":%d]")\
$DEF_MSG(N2S, SUCCESS,	NETDISCN,		"Disconnect client connection [%s, " IPv4_BYTES_FMT ":%d ->" IPv4_BYTES_FMT ":%d]")\
$DEF_MSG(N2S, ERROR,	NETERROR,		"Disconnect client connection [%s, " IPv4_BYTES_FMT ":%d ->" IPv4_BYTES_FMT ":%d] --- errno: %d")\
$DEF_MSG(N2S, SUCCESS,	FULLMBAP,		"Whole MBAP packet in buffer")\
\
$DEF_MSG(N2S, WARN,	RNF,			"Record not found") \
$DEF_MSG(N2S, WARN,	NOLAN,			"No active LAN port has been established") \
\
\
$DEF_MSG(N2S, ERROR,	LINKERR,		"<%.*s> [Device: <%.*s>, PE%02d:] --- code: %d (%s)")\
$DEF_MSG(N2S, ERROR,	CRCERR,			"CRC error is detected")\
\
\
$DEF_MSG(N2S, INFO,	REVISION,		"%s") \
$DEF_MSG(N2S, INFO,	LOGFILE,		"Redirect STDOUT/STDERR to: '%.*s'") \





enum						/* Generate messages number constants */
{
	#define	$DEF_MSG(f, s, c, t)	f##$__##c = ( (f << 16) | (__COUNTER__<< 3) | STS$K_##s),
	__DEF_MESSAGES__
	#undef	$DEF_MSG

};


#ifdef __cplusplus
}
#endif

#endif	/*	__N2S_MSGS____	*/

