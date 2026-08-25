## General memories


###	Quick check

####	Terminal session 1
```
root@napic:~/Works/net2serial# ./build/net2serial /trace /settings=./net2serial_settings.conf
25-08-2026 13:26:11.101   1024 [N2S-MAIN\\main:781] %N2S-I-REVISNF, Rev: NET2SER X.00-05/aarch64(built at Aug 25 2026 13:18:09 with CC 13.3.0) (REV: 00.05.00)
25-08-2026 13:26:11.101   1024 [UTIL$\\__util$showparams:670] %UTILS-I:  trace = ON
25-08-2026 13:26:11.101   1024 [N2S-MAIN\\s_settings_process_serials:411] %NET2SER-I:  Added device #00 [</dev/ttyUSB0>, Chars: <9600, 8, N, 1>, Flow: <NONE>, I/O Tmo: 5000 msec] --- added
25-08-2026 13:26:11.101   1024 [N2S-MAIN\\s_settings_process_listeners:574] %NET2SER-I:  Added listener #00 [Target: </dev/ttyUSB0>, Net: <TCP:0.0.0.0:502>, I/O Tmo: 5000 msec, Backlog: 8] --- added
25-08-2026 13:26:11.102   1024 [N2S-TTY\\n2s$tty_open:684] %N2S-S-DEVREADY, Device </dev/ttyUSB0> [9600 baud, 8N1, flow: NONE] --- is ready
25-08-2026 13:26:11.102   1024 [N2S-NET\\n2s$net_start_listeners:612] %N2S-S-LSNRRDY, [#3] Listener 0.0.0.0:502 [Target: </dev/ttyUSB0>] --- is ready
25-08-2026 13:26:33.765   1025 [N2S-NET\\s_net_listener:432] %N2S-S-NETCONN, [#5] Accept client connection 127.0.0.1:55218 (listener #3)
25-08-2026 13:26:35.631   1026 [N2S-TTY\\n2s$tty_tx:190] [#4:</dev/ttyUSB0>] Sent 2 octets (from 2)
25-08-2026 13:26:41.543   1026 [N2S-TTY\\n2s$tty_tx:190] [#4:</dev/ttyUSB0>] Sent 7 octets (from 7)
25-08-2026 13:27:02.118   1026 [N2S-NET\\s_net_session:353] %N2S-S-NETDISCN, [#5] Disconnect client connection 127.0.0.1:55218
```

####	Terminal session 2
```
root@napic:~# telnet 127.0.0.1 502
Trying 127.0.0.1...
Connected to 127.0.0.1.
Escape character is '^]'.


ddddd
123

```

####	A second client of a busy port
```
root@napic:~# telnet 127.0.0.1 502
Trying 127.0.0.1...
Connected to 127.0.0.1.
Escape character is '^]'.
Connection closed by foreign host.
```
and in the gateway log:
```
%N2S-W-DEVBUSY, [#6] Reject client 127.0.0.1:55220 --- device </dev/ttyUSB0> is busy with the session #5
```
