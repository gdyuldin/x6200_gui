#!/usr/bin/env python3
"""A simulated X6200 base for base_flash, on this computer: base_flash.c built with its port on a pseudo-terminal and
its I2C bus on an empty file (the I2C commands then fail, as on a base in its bootloader), run as on the radio
(--flash FILE --yes --recover --held-by, as the GUI runs it), against a fake base that streams like the base's application, answers as Xiegu's
bootloader does, or fails in one of the ways Xiegu's app has an answer for. tools/base_update/test/run.sh runs them all.

    sim.py CC SRC STUB_DIR WORK_DIR XGF MODE [TAG]

Prints one line, "PASS ..." or "FAIL ...", and exits 0 or 1.

SPDX-License-Identifier: LGPL-2.1-or-later
"""
import binascii
import os
import pty
import select
import subprocess
import sys
import threading
import time
import tty

# mode: (exit status, a line base_flash must log, the bytes received == the file)
MODES = {
    'ok':      (0, '1k xmodem send finished.', True),
    'silent':  (0, '[ERR] Wait IAP respond timeout', True),     # Xiegu's app goes on after it: so must base_flash
    'stream':  (0, '[ERR] Clear rx buffer timeout', True),      # likewise
    'dead':    (2, '[ERR] IAP failed, connect timeout', False),
    'noerase': (3, "[ERR] IAP failed, can't erase flash", False),
    'nostart': (3, '1k xmodem timeout.', False),
    'nak':     (3, '1k xmodem bad packet, pn= 100', False),
    'can':     (3, '1k xmodem transfer canceled. (packet 50', False),
    'garbage': (3, '1k xmodem error, = 88 (packet 10', False),
    'eot':     (3, '1k xmodem error, = 0 (after EOT)', True),
    'x6100':   (1, 'refused: this radio is not an X6200', False),  # another radio: nothing at all sent to its base
}

cc, src, stub, work, xgf_path, mode = sys.argv[1:7]
tag = sys.argv[7] if len(sys.argv) > 7 else mode
want_rc, want_line, want_data = MODES[mode]
xgf = open(xgf_path, 'rb').read()
m, s = pty.openpty()
tty.setraw(s)
binp = os.path.join(work, 'sim_' + tag)
logp = os.path.join(work, 'sim_' + tag + '.log')
dtp = os.path.join(work, 'dt_' + tag)              # the device tree's compatible: an X6200's, or another radio's
with open(dtp, 'wb') as f:
    f.write((b'xiegu,x6100' if mode == 'x6100' else b'xiegu,x6200') + b'\0allwinner,sun8i-r16\0')
# The I2C bus: an empty file of this scenario's own, where its ioctls fail as on a base in its bootloader (not
# /dev/null, which every process on Linux holds: the writer would see them all as holders of its bus)
i2cp = os.path.join(work, 'i2c_' + tag)
open(i2cp, 'wb').close()
subprocess.run([cc, '-O1', '-w', '-I', stub, '-DUART="%s"' % os.ttyname(s), '-DI2C_BUS="%s"' % i2cp, '-DBACK_WAIT_S=2',
                '-DDT_COMPAT="%s"' % dtp, '-o', binp, src], check=True)
log = open(logp, 'w')
# This process holds the port's other end, as the GUI does when it lends the base: --held-by says so (on Linux the
# writer sees every holder in /proc, and would refuse another)
p = subprocess.Popen([binp, '--flash', xgf_path, '--yes', '--recover', '--held-by', str(os.getpid())], stdout=log,
                     stderr=subprocess.STDOUT)


def rd(n, t):
    b = b''
    end = time.time() + t
    while len(b) < n and time.time() < end:
        r, _, _ = select.select([m], [], [], 0.02)
        if r:
            b += os.read(m, n - len(b))
    return b


def logged(text):
    try:
        return text in open(logp).read()
    except OSError:
        return False


t0 = time.time()
state, asks, got, seq = 'app', 0, bytearray(), 1


def stream():
    """An application that keeps sending and never hands over: blocking writes keep the pseudo-terminal's buffer full,
    so that every one of base_flash's reads finds something, until base_flash has given up waiting"""
    while p.poll() is None and not logged(want_line) and time.time() - t0 < 60:
        os.write(m, b'\x55' * 1024)


if mode == 'stream':
    threading.Thread(target=stream, daemon=True).start()
sent = b''
while p.poll() is None:
    if mode == 'x6100':                         # base_flash must not open the port: anything it sends is caught here
        sent += rd(4096, 0.05)                  # (nothing is written to it: no reader, and a full buffer would block)
        continue
    if mode == 'dead':                          # nothing ever: the base gone
        rd(4096, 0.1)
        continue
    if mode in ('silent', 'stream') and not logged(want_line) and time.time() - t0 < 60:
        if mode == 'silent':                    # the 'c's go unanswered until base_flash gave up waiting
            rd(4096, 0.01)
        else:
            time.sleep(0.01)                    # base_flash reads what the stream thread writes
        continue
    if state == 'app' and mode not in ('silent', 'stream'):
        os.write(m, b'\x55' * 64)               # the application's stream until it has been asked twice
        r, _, _ = select.select([m], [], [], 0.003)
        if r and b'c' in os.read(m, 4096):
            asks += 1
            state = 'boot' if asks >= 2 else 'app'
        continue
    state = 'boot' if state == 'app' else state
    b = rd(1, 0.5)
    if not b:
        continue
    if state == 'boot':
        if b == b'c':
            os.write(m, b'y')
        elif b == b'r' and mode != 'noerase':
            time.sleep(1.5)                     # an erase takes a while
            os.write(m, b'g')
        elif b == b'p':
            os.write(m, b'g')
            time.sleep(0.2)
            if mode != 'nostart':
                os.write(m, b'CC')
            state = 'xmodem'
    elif state == 'xmodem':
        if b == b'\x04':
            if mode != 'eot':
                os.write(m, b'\x06')
            continue
        pkt = b + rd(1028, 5)
        ok = len(pkt) == 1029 and pkt[0] == 2 and pkt[1] == seq & 0xff and pkt[2] == (~seq) & 0xff and \
            pkt[1027:] == binascii.crc_hqx(pkt[3:1027], 0).to_bytes(2, 'big')
        if (mode, seq) == ('nak', 100):
            os.write(m, b'\x15')
        elif (mode, seq) == ('can', 50):
            os.write(m, b'\x18')
        elif (mode, seq) == ('garbage', 10):
            os.write(m, b'X')
        else:
            os.write(m, b'\x06' if ok else b'\x15')
            if ok:
                got += pkt[3:1027]
                seq += 1
log.close()
same = bytes(got) == xgf + b'\xff' * (-len(xgf) % 1024)
sent += rd(4096, 0.1) if mode == 'x6100' else b''
good = p.returncode == want_rc and logged(want_line) and same == want_data and not (mode == 'x6100' and sent)
print('%s %-8s exit %d (want %d), %3d packets, data %s, "%s" %s, %.0f s' % (
    'PASS' if good else 'FAIL', tag, p.returncode, want_rc, seq - 1, '== file' if same else '!= file',
    want_line, 'logged' if logged(want_line) else 'NOT logged', time.time() - t0))
sys.exit(0 if good else 1)
