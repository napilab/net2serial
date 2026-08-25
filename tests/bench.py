#!/usr/bin/env python3
# Self-contained bench for net2serial: a pty pair + a fake serial device + the gateway + TCP clients.
# Usage: python3 tests/bench.py [<path to net2serial>]
import os, pty, tty as ttymod, time, socket, subprocess, threading, sys, signal

GW = sys.argv[1] if len(sys.argv) > 1 else './build/net2serial'

# --- a pty pair: the gateway takes the slave side, the bench keeps the master one ---
m1, s1 = pty.openpty()
ttymod.setraw(m1); ttymod.setraw(s1)
gw_dev = os.ttyname(s1)
print("gateway tty:", gw_dev)

conf = f'''listeners = (
	{{ bind = "TCP:127.0.0.1:5599"; connlm = 8; iotmo = 3000; target = "{gw_dev}"; }}
);
serials = (
	{{ desc = "bench device"; device = "{gw_dev}"; chars = "115200, 8, N, 1"; flow = "NONE"; iotmo = 3000; rs485 = 0; }}
);
'''
open('/tmp/n2s_bench.conf','w').write(conf)

# --- the fake device: echoes back everything in upper case, plus can push unsolicited data ---
push = {'q': b''}
stop = {'v': False}
def device():
    while not stop['v']:
        try:
            chunk = os.read(m1, 4096)
        except OSError:
            return
        if not chunk:
            continue
        os.write(m1, chunk.upper())

threading.Thread(target=device, daemon=True).start()

gwp = subprocess.Popen([GW, '/trace', '/settings=/tmp/n2s_bench.conf'],
                       stdout=open('/tmp/n2s_gw.log','w'), stderr=subprocess.STDOUT)
time.sleep(1.2)
assert gwp.poll() is None, "gateway died: " + open('/tmp/n2s_gw.log').read()[-800:]

def recv_all(s, n, tmo=5.0):
    s.settimeout(tmo)
    buf = b''
    t0 = time.time()
    while len(buf) < n and (time.time() - t0) < tmo:
        try:
            b = s.recv(n - len(buf))
        except socket.timeout:
            break
        if not b: break
        buf += b
    return buf

ok = True
try:
    # T1: a round trip through the serial line
    c = socket.create_connection(('127.0.0.1', 5599), timeout=5)
    c.sendall(b'hello serial\n')
    r = recv_all(c, len(b'hello serial\n'))
    print("T1 echo:", r)
    assert r == b'HELLO SERIAL\n', "T1 round trip"
    print("T1 PASS (octets pass through the serial line in both directions)")

    # T2: a big block - the partial write() path (N2S-02)
    blob = bytes((0x41 + (i % 26)) for i in range(60000))
    c.sendall(blob)
    r = recv_all(c, len(blob), tmo=25.0)
    print("T2 sent:", len(blob), "got back:", len(r))
    assert len(r) == len(blob), f"T2 lost {len(blob)-len(r)} octets (partial write)"
    assert r == blob.upper(), "T2 corrupted"
    print("T2 PASS (60000 octets survived, no partial-write loss)")

    # T3: a second client of a busy line is rejected (N2S-09)
    c2 = socket.create_connection(('127.0.0.1', 5599), timeout=5)
    time.sleep(0.7)
    d = recv_all(c2, 1, tmo=1.5)
    closed = (d == b'')
    print("T3 second client got:", d, "closed:", closed)
    assert closed, "T3 the second client was not rejected"
    log = open('/tmp/n2s_gw.log').read()
    assert 'DEVBUSY' in log, "T3 no DEVBUSY diagnostic"
    print("T3 PASS (second client rejected with DEVBUSY, first one keeps working)")

    # first client still alive after the rejection
    c.sendall(b'still here\n')
    r = recv_all(c, len(b'still here\n'))
    assert r == b'STILL HERE\n', "T3b first session broken"
    print("T3b PASS (the owning session is intact)")

    c.close(); c2.close()
    time.sleep(0.8)

    # T4: the line is released - a new client may take it
    c3 = socket.create_connection(('127.0.0.1', 5599), timeout=5)
    c3.sendall(b'next client\n')
    r = recv_all(c3, len(b'next client\n'))
    assert r == b'NEXT CLIENT\n', "T4 the line was not released"
    print("T4 PASS (the line is released at the disconnect and reused)")
    c3.close()
    time.sleep(0.5)

    # T5: idle session does not eat the CPU (N2S-05/N2S-03 sanity)
    c4 = socket.create_connection(('127.0.0.1', 5599), timeout=5)
    def cpu_ticks(pid):
        f = open(f'/proc/{pid}/stat').read().split()
        return int(f[13]) + int(f[14])
    t0 = cpu_ticks(gwp.pid); time.sleep(3.0); t1 = cpu_ticks(gwp.pid)
    print("T5 CPU ticks over 3 idle seconds:", t1 - t0)
    assert (t1 - t0) < 10, "T5 busy loop on an idle session"
    print("T5 PASS (an idle session consumes no CPU)")
    c4.close()
    time.sleep(0.8)

    # T6: thread hygiene - 40 short sessions, the thread count must not grow (N2S-19)
    for i in range(40):
        x = socket.create_connection(('127.0.0.1', 5599), timeout=5); x.close(); time.sleep(0.02)
    time.sleep(1.5)
    nthreads = len(os.listdir(f'/proc/{gwp.pid}/task'))
    print("T6 threads after 40 sessions:", nthreads)
    assert nthreads < 10, "T6 thread leak"
    print("T6 PASS (no thread leak)")

    print("ALL PASS")
except Exception as e:
    ok = False
    print("FAIL:", e)
    print("--- gw.log tail ---")
    print(open('/tmp/n2s_gw.log').read()[-2500:])
finally:
    stop['v'] = True
    gwp.send_signal(signal.SIGTERM); time.sleep(0.4)
    gwp.send_signal(signal.SIGTERM); time.sleep(0.6)
    if gwp.poll() is None: gwp.kill()

sys.exit(0 if ok else 1)
