## General memories


###	Quick check

####	Terminal session 1
```
root@napic:~/Works/net2serial# ./build/net2serial /trace /settings=./net2serial_settings.conf
24-03-2026 13:29:36.827  41917 [N2S-MAIN\main:481] %NET2SER-I:  Rev: X.00-02/aarch64, (built  at Mar 24 2026 13:23:23 with CC 13.3.0)
24-03-2026 13:29:36.830  41917 [UTIL$\__util$showparams:670] %UTILS-I:  trace = ON
24-03-2026 13:29:36.830  41917 [UTIL$\__util$showparams:693] %UTILS-I:  logfile[0:0] =''
24-03-2026 13:29:36.830  41917 [UTIL$\__util$showparams:687] %UTILS-I:  logsize = 0 (0)
24-03-2026 13:29:36.830  41917 [UTIL$\__util$showparams:693] %UTILS-I:  settings[0:26] ='./net2serial_settings.conf'
24-03-2026 13:29:36.830  41917 [N2S-MAIN\s_settings_process_serials:259] %NET2SER-I:  Added device #00 [</dev/ttyS0>, Chars: <115200, 8, N, 1>] --- added
24-03-2026 13:29:36.830  41917 [N2S-MAIN\s_settings_process_listeners:346] %NET2SER-I:  Added listener #00 [Target: </dev/ttyS0>, Net: <TCP:0.0.0.0:502>, I/O Tmo: 5000 msec, Connection limit: 8] --- added
24-03-2026 13:29:36.830  41917 [N2S-MAIN\s_init_sig_handler:454]        Set handler for signal 15/0xf (Terminated)
24-03-2026 13:29:36.830  41917 [N2S-MAIN\s_init_sig_handler:454]        Set handler for signal 2/0x2 (Interrupt)
24-03-2026 13:29:36.830  41917 [N2S-MAIN\s_init_sig_handler:454]        Set handler for signal 10/0xa (User defined signal 1)
24-03-2026 13:29:36.830  41917 [N2S-MAIN\s_init_sig_handler:454]        Set handler for signal 3/0x3 (Quit)
24-03-2026 13:29:36.830  41917 [N2S$-NET\n2s$net_start_listeners:380] %NET2SER$-S:  [#3] Listener [0.0.0.0:502, Target: <#4:/dev/ttyS0>] --- initialized
24-03-2026 13:31:33.765  41918 [N2S$-NET\s_net_listener:276]            [#3] Accept connection from 127.0.0.1:55218 on SD: #5
24-03-2026 13:31:33.768  42052 [N2S$-NET\s_net_session:158] %NET2SER$-I:  [#5] Start session for 127.0.0.1:55218 ....
24-03-2026 13:31:35.631  42052 [T2R-TTY\n2s$tty_tx:128]                 [#4:</dev/ttyS0>] Sent 2 octets (from 2)
24-03-2026 13:31:41.543  42052 [T2R-TTY\n2s$tty_tx:128]                 [#4:</dev/ttyS0>] Sent 2 octets (from 2)
24-03-2026 13:31:44.574  42052 [T2R-TTY\n2s$tty_tx:128]                 [#4:</dev/ttyS0>] Sent 7 octets (from 7)
24-03-2026 13:31:51.246  42052 [T2R-TTY\n2s$tty_tx:128]                 [#4:</dev/ttyS0>] Sent 5 octets (from 5)

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
