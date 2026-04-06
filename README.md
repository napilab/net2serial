# Net-to-Serial
A yet another gateway pass transparently octets stream over TCP-connection to a Serial port on running this gateway host


###	NAPI World
This project is developed and maintained by the NAPI Lab team
and is primarily tested on NAPI industrial single-board computers based on Rockchip SoCs.

####	NAPI Boards

If you are looking for a reliable hardware platform to run modbus_slave in production,
check out the NAPI board lineup:

Welcome to NAPI Wolrd (https://github.com/napilab/napi-boards) for more information!

Right now is available:

- NAPI2 — RK3568J, RS-485 onboard, Armbian
- NAPI-C — RK3308, compact, industrial grade




###	Introduction

A simple TCP-to-Serial (Ethernet-to-Serial, Network-to-Serial) gateway to pass octets stream from remote TCPclinet to a local
OS-hosted serial device. This  is supposed to be as a tutorial:

 - a development of programming skills
 - a programming for I/O on  RS323 and RS-485 ports
 - basic checking of work of MODBUS-capable devices
 - build two-directionional gateways for TCP-to-RTU and RTU-to-TCP


### 	Build from sources

```
$ git clone  <URL>
$ cd mbus-gw-t2r
$ git submodule update --init --recursive

$ mkdir build
$ cd build
$ cmake ../CMakeLists.txt -B ./
```
 or

```
$ cmake ../CMakeLists.txt -DCMAKE_BUILD_TYPE=Debug -B ./
$ make -s
```

### 	Quick configuration

A configuration option is a single line text string  starting with "-" or "/",  option case is not matter :

`/<option_name>=<option_value>`

or

`-<option_name>=<option_value>`

Example:


-logfile=/tmp/ttr.log
##### CLI options

| Option		|  Description
| ------		| ------------------------------------------------------------
| trace			| Enable extensible diagnostic output. Useful for for debug and troubleshouting purpose.
| logfile=\<fpsec\>	| Set a file name to accept logging output
| logsize=\<number\>	| Limit size of log file.
| settings=\<fspec\>	| Provide a rin-time configuration for network stuff and serial devices


##### Settings options
Check an example of settings file for reference of parameters and rules of configurations

## Authors and acknowledgment

Developer: Ruslan (AKA : The BadAss SysMan) Laishev
VAX/VMS bigot,
BMF.
