# net2serial --- User Guide (English)

*A network-to-serial gateway: it passes a raw octets stream between a TCP connection and a
local serial port. This guide is written for a beginner: every step is spelled out, nothing is
assumed. The Russian version of this guide is in `UserGuide_RU.md`.*

---

## 1. What this program does

You have a device with a serial console or a serial interface --- a switch, a router, an old
VAX, a PLC, a UPS. It is plugged into the serial port of the computer where this gateway runs.
And you want to reach that device over the network, from your desk.

The gateway sits between them and passes the octets through, in both directions, without
touching them:

```
 [telnet / nc / a terminal] --- TCP/IP network ---> [net2serial] --- serial line ---> [Your device]
        (your desk)                                (this program)  (RS-232/RS-485)   (console, PLC...)
```

Nothing is interpreted: what you type goes to the device as is, what the device prints comes
back as is. This is why it works with anything --- a login prompt, a bootloader, a binary
protocol.

**One client at a time per port.** An octets stream has no framing: if two people typed into
one console at once, their characters would interleave and neither would get a readable answer.
So a serial port is given to a single session; a second client is refused with a clear message
and the first one keeps working undisturbed. As soon as the first client disconnects, the port
is free for the next one.

## 2. Before you start --- the checklist

1. The gateway is installed (see `README.md`, section *Installation*). After the installation
   you have the program `/usr/local/sbin/net2serial` and the settings file
   `/usr/local/etc/net2serial/net2serial_settings.conf`.
2. You know which serial port your device is connected to. On Linux it is a file like
   `/dev/ttyS0` or `/dev/ttyUSB0`. If unsure, plug the USB adapter out and in, then run:

```
$ dmesg | tail
```

3. You know the line parameters of the device: the speed, the data bits, the parity, the stop
   bits and the flow control. They are in the device manual. The most common console set is
   `9600, 8, N, 1` with no flow control.
4. Your user may open the port:

```
$ ls -l /dev/ttyUSB0
crw-rw---- 1 root dialout 188, 0 ... /dev/ttyUSB0
```

If the group is `dialout`, add yourself to it and re-login (or run the gateway with `sudo`):

```
$ sudo usermod -a -G dialout $USER
```

## 3. The settings file

Two sections: `serials` (the serial ports) and `listeners` (the TCP ports). A minimal working
example:

```
serials = (
	{	device = "/dev/ttyUSB0";
		chars  = "9600, 8, N, 1";
		flow   = "NONE";
	}
);

listeners = (
	{	bind   = "TCP:0.0.0.0:5000";
		target = "/dev/ttyUSB0";
	}
);
```

Read it as: *"open /dev/ttyUSB0 at 9600-8-N-1 with no flow control; listen for TCP clients on
every interface, port 5000, and connect them to that port"*.

### 3.1. The `serials` section --- one record per serial port

| Key | Required | Meaning and the allowed values |
|-----|----------|--------------------------------|
| `device` | **yes** | The serial port file, e.g. `/dev/ttyUSB0` |
| `chars` | **yes** | `speed, data bits, parity, stop bits`. Speed 50..4000000; data bits 5..8; parity `N` (none), `E` (even), `O` (odd); stop bits 1..2 |
| `flow` | no | Flow control: `NONE`, `XON/XOFF`, `RTS/CTS`. Default `NONE` |
| `iotmo` | no | The I/O timeout, milliseconds, 1..600000. Default 3000 |
| `rs485` | no | `1` --- ask the kernel to drive the RS-485 direction control. Default `0` |
| `desc` | no | A free text description, for your own convenience |

**Which flow control to pick?** If the device manual says nothing --- `NONE`. A Cisco-style
console cable usually wants `RTS/CTS`. Old terminals and printers often want `XON/XOFF`. If you
paste a long text and the tail comes out garbled, the flow control is the first thing to check.

If a record is wrong, the gateway skips it and says why --- with the allowed range right in the
message:

```
%NET2SER-E:  [serial #00:</dev/ttyUSB0>] --- speed 31 baud is out of range [50..4000000]
```

### 3.2. The `listeners` section --- one record per TCP port

| Key | Required | Meaning and the allowed values |
|-----|----------|--------------------------------|
| `bind` | **yes** | Where to listen: `TCP:<IP address>:<port>`. Port 1..65535; `0.0.0.0` means every interface. UDP is not supported |
| `target` | **yes** | Which serial port to connect to. Must match a `device` from `serials` exactly, character by character |
| `connlm` | no | The backlog of the listening socket, 1..128. This is **not** a count of simultaneous clients --- see Section 1 |
| `iotmo` | no | The network I/O timeout, milliseconds, 1..600000 |

Ports below 1024 need root on Linux. If you do not want to run as root, take e.g. `5000` and
point your client there.

## 4. Running the gateway

```
$ /usr/local/sbin/net2serial /settings=/usr/local/etc/net2serial/net2serial_settings.conf
```

| Option | Meaning |
|--------|---------|
| `/settings=<file>` | The settings file (Chapter 3) |
| `/trace` | Verbose tracing: every chunk of octets is reported. Priceless at the first run |
| `/logfile=<file>` | Write the log to a file instead of the screen |
| `/logsize=<octets>` | Rotate the log file above this size |

A healthy start prints (shortened):

```
%N2S-I-REVISNF, Rev: NET2SER X.00-05/aarch64(built at ...) (REV: 00.05.00)
%NET2SER-I:  Added device #00 [</dev/ttyUSB0>, Chars: <9600, 8, N, 1>, Flow: <NONE>, ...] --- added
%NET2SER-I:  Added listener #00 [Target: </dev/ttyUSB0>, Net: <TCP:0.0.0.0:5000>, ...] --- added
%N2S-S-DEVREADY, Device </dev/ttyUSB0> [9600 baud, 8N1, flow: NONE] --- is ready
%N2S-S-LSNRRDY, [#3] Listener 0.0.0.0:5000 [Target: </dev/ttyUSB0>] --- is ready
```

Two lines matter most: **DEVREADY** (the port is open) and **LSNRRDY** (the TCP port is
listening). If you see both, the gateway is up.

To stop it: press Ctrl/C once and wait a second. To toggle the tracing of a *running* gateway:

```
$ kill -USR1 <pid>
```

### 4.1. The first test

```
$ telnet 127.0.0.1 5000
```

Press Enter a couple of times --- a console device usually answers with its prompt. `nc` works
too and is friendlier for binary data:

```
$ nc 127.0.0.1 5000
```

## 5. Reading the log

```
%N2S-E-DEVOPNERR, Cannot open the device </dev/ttyUSB0>, errno: 2
 ^   ^  ^
 |   |  +-- the message code: look it up in Section 6.2
 |   +----- the severity: S=success, I=info, W=warning, E=error, F=fatal
 +--------- the facility (always N2S for this program)
```

Grep the log by the code --- this is exactly why the codes exist:

```
$ grep DEVBUSY /var/log/net2serial.log
```

## 6. Troubleshooting

**The golden rule: run with `/trace` and read the log. The gateway always says what it
dislikes.**

### 6.1. The symptom, the cause, the action

| You see | It means / what to do |
|---------|------------------------|
| `%N2S-E-DEVOPNERR, ... errno: 2` | The port file does not exist: the adapter is unplugged or the name is wrong. Run `dmesg \| tail` after plugging it in; fix `device` in the settings |
| `%N2S-E-DEVOPNERR, ... errno: 13` | Permission denied. Run with `sudo`, or add yourself to the `dialout` group |
| `%N2S-E-DEVOPNERR, ... errno: 16` | The port is busy: another program holds it. Find it: `sudo fuser /dev/ttyUSB0` |
| `%N2S-W-DEVBUSY, ... is busy with the session #N` | Somebody is already working with that port. This is by design (Section 1). Wait, or ask the colleague to disconnect |
| `%N2S-E-LSNRERR, ... bind() error, errno: 98` | The TCP port is taken: a second gateway instance or another program. Find it: `sudo ss -tlnp \| grep 5000` |
| `%N2S-E-LSNRERR, ... errno: 13` | Ports below 1024 need root. Run with `sudo`, or take a port above 1024 |
| `%N2S-E-LINKDOWN, ... Serial line failure` | The serial port has died under a live session --- almost always an unplugged USB adapter. Plug it back; the session is closed, just reconnect |
| `%N2S-W-SESSTMO, No activity for 1200 seconds` | The session was idle for 20 minutes and was closed. Only truly idle sessions are closed --- any traffic resets the timer |
| You connect, but nothing comes back | 1) The wrong speed or parity --- re-check `chars`; 2) the wiring (a null-modem cable is often needed); 3) the device is off or has nothing to say --- press Enter |
| The output is garbage characters | Almost always the wrong speed. Try 9600, 19200, 38400, 115200 in turn |
| A long paste comes out truncated or scrambled | The device cannot keep up: set `flow = "RTS/CTS"` or `"XON/XOFF"` per the device manual |
| `... out of range [a..b]` | A settings value is out of its range; the allowed range is right in the message |
| `No serials has been defined!` | Not a single `serials` record survived the validation. Read the error lines above --- each rejected record says why |

### 6.2. The message codes reference

| Code | Sev | When it appears |
|------|-----|-----------------|
| `REVISNF` | I | At the start: the program version. Quote it when asking for help |
| `DEVREADY` | S | The serial port is open and configured; the actual line parameters are shown |
| `LSNRRDY` | S | The TCP port is listening; the address, the port and the target device are shown |
| `DEVOPNERR` | E | The serial port cannot be opened; `errno` says why (2 = no such file, 13 = permissions, 16 = busy) |
| `LSNRERR` | E | `bind()`/`listen()` failed for a TCP port; `errno` says why (98 = taken, 13 = privileges) |
| `NETCONN` | S | A client has connected; its address:port and the listener are shown |
| `NETDISCN` | S | A client has disconnected; its address:port is shown |
| `DEVBUSY` | W | A client was rejected: the port is already owned by another session |
| `SESSTMO` | W | A session was closed after being idle for too long |
| `NETIOERR` | E | A network I/O error; the failed call and the `errno` are shown |
| `TTYIOERR` | E | A serial I/O error; the failed call and the `errno` are shown |
| `LINKDOWN` | E | The serial line failed under a live session (an unplugged adapter) |
| `EXITST` | I | The gateway exits; the exit flag and the final status are shown |

### 6.3. If nothing helps

Collect and attach to your question: 1) the full start-up log with `/trace` (from `REVISNF` to
the first error); 2) your settings file; 3) the output of `ls -l <device>` and `dmesg | tail -20`;
4) the exact model of the device and its documented line parameters. With these four things the
problem is almost always visible at a glance.
