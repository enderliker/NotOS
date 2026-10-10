#!/usr/bin/env python3
"""Artifact basename regression: pure host checks, no QEMU or guest network."""
import ast
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from harness import Machine,validate_logname

class LogNames(unittest.TestCase):
    def test_actual_socket_cases(self):
        source=ast.parse(Path('tests/sockets.py').read_text())
        assignment=next(node for node in source.body if isinstance(node,ast.Assign)
                        and any(isinstance(target,ast.Name) and target.id=='UNAVAILABLE_CASES' for target in node.targets))
        cases=ast.literal_eval(assignment.value)
        self.assertEqual(cases, (
            ('sockets-no-nic',{'network':False}),
            ('sockets-no-entropy',{'network':True,'entropy':False}),
        ))
        with tempfile.TemporaryDirectory() as directory:
            for name,options in cases:
                self.assertEqual(validate_logname(name),name)
                path=Path(directory,name+'.log');path.write_text('PASS\n')
                self.assertEqual(path.read_text(),'PASS\n')
                self.assertFalse(set('<>:"/\\|?*\r\n') & set(path.name))
                self.assertIn('network',options)

    def test_reject_unsafe_names_before_starting_qemu(self):
        unsafe=["sockets-unavailable-{'network': False}",'', '.', '..', '../log', 'dir/log']
        unsafe += ['log'+character+'name' for character in '<>:"\\|?*\r\n\0']
        with patch('harness.subprocess.Popen') as start,patch('harness.tempfile.TemporaryDirectory') as temporary:
            for name in unsafe:
                with self.subTest(name=name),self.assertRaises(ValueError):Machine(logname=name)
            start.assert_not_called();temporary.assert_not_called()

    def test_existing_name_patterns(self):
        for name in ['smoke','sockets-16M','sockets-64M','sockets-dns','https-date-2050',
                     'tls-platform-False','no-nx-max--nx','vfs-notfs-reboot']:
            self.assertEqual(validate_logname(name),name)

if __name__=='__main__':unittest.main(verbosity=2)
