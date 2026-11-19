#!/usr/bin/env python3
# AirESP32ace
# Created by VM, November 2026
"""Host-only tests; no USB, filesystem data, pairing or pyserial needed."""
from pathlib import Path
import os
import pty
import runpy
import select
import termios
import threading
import time
import unittest

harness = runpy.run_path(str(Path(__file__).with_name("soak-device.py")))
OutageWitness = harness["OutageWitness"]
PassiveHealthPort = harness["PassiveHealthPort"]

class PassiveHealthPortTests(unittest.TestCase):
    def setUp(self):
        self.master, slave = pty.openpty()
        self.addCleanup(os.close, self.master)
        self.addCleanup(os.close, slave)
        self.port = PassiveHealthPort(os.ttyname(slave))
        self.addCleanup(self.port.close)

    def test_fragmented_health_only_no_echo_or_hangup(self):
        attrs = termios.tcgetattr(self.port.fd)
        self.assertFalse(attrs[2] & termios.HUPCL)
        self.assertFalse(attrs[3] & (termios.ECHO | termios.ICANON))
        read_command = []
        def emulate():
            if not select.select([self.master], [], [], 1)[0]: return
            read_command.append(os.read(self.master, 64))
            for chunk in [b"log: init\r\nASTCFG:OK:{}\n", b"x"*4096, b"x"*300,
                          b"ASTCFG:OK:bad\nASTCFG:OK:not-json\nAST", b"CFG:OK:{",
                          b'"bootId":42,"elfSHA256":"stub","uiHeartbeat":5}\r', b"\n"]:
                os.write(self.master, chunk)
                time.sleep(0.005)
        worker = threading.Thread(target=emulate)
        worker.start()
        try:
            self.assertEqual(self.port.health(timeout=2)["bootId"], 42)
        finally:
            worker.join(2)
        self.assertEqual(read_command, [b"ASTCFG:HEALTH\n"])
        self.assertFalse(select.select([self.master], [], [], 0.05)[0])  # no echo

    def test_incomplete_reply_has_bounded_deadline(self):
        os.write(self.master, b'ASTCFG:OK:{"bootId":')
        with self.assertRaises(TimeoutError):
            self.port.health(timeout=0.08)

class OutageWitnessTests(unittest.TestCase):
    def observe(self, *samples, minimum=120):
        witness = OutageWitness(minimum)
        for sample in samples:
            witness.observe(*sample)
        return witness

    def test_one_sample_and_long_stale_online_wait_do_not_qualify(self):
        w = self.observe((0, True, 2), (15, False, 2), (150, True, 2), (300, True, 3))
        self.assertFalse(w.recovered)
        self.assertFalse(w.pending)
        self.assertEqual(w.longest_recovered, 0)

    def test_distinct_outages_cannot_accumulate(self):
        w = self.observe((0, True, 1), (10, False, 1), (90, False, 1), (100, True, 1),
                         (110, False, 1), (190, False, 1), (200, True, 1), (210, True, 2))
        self.assertFalse(w.recovered)
        self.assertEqual(w.longest_recovered, 80)

    def test_qualifying_reconnect_requires_subsequent_new_data(self):
        w = self.observe((0, True, 8), (10, False, 8), (70, False, 8), (130, False, 8),
                         (145, True, 9), (200, True, 9))
        self.assertTrue(w.pending)
        self.assertFalse(w.recovered)
        w.observe(220, True, 10)
        self.assertTrue(w.recovered)
        self.assertFalse(w.pending)
        self.assertEqual(w.longest_recovered, 120)

    def test_booting_offline_and_timestamp_regression(self):
        w = self.observe((0, False, 10), (130, False, 10), (150, True, 1), (175, True, 2))
        self.assertTrue(w.pending)
        w.observe(200, True, 11)
        self.assertFalse(w.pending)
        self.assertFalse(w.recovered)

    def test_cannot_count_qualifying_outage_if_interrupted_before_freshness(self):
        w = self.observe((0, True, 1), (10, False, 1), (150, False, 1), (160, True, 1),
                         (180, False, 1), (185, True, 1), (210, True, 2))
        self.assertFalse(w.recovered)

    def test_nondefault_threshold_and_unresolved_second_outage(self):
        w = self.observe((0, True, 5), (10, False, 5), (25, False, 5), (30, True, 5),
                         (37, True, 6), (45, False, 6), minimum=15)
        self.assertTrue(w.recovered)
        self.assertTrue(w.pending)

if __name__ == "__main__":
    unittest.main()
