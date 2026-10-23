"""Interpret bootstrap ownership with injected Vita module/network failures; no native calls."""
import unittest

from pycparser import c_parser
from test_host_persistence import function, run
from test_xml_source import ROOT, strip_directives


class NetworkLifecycle(unittest.TestCase):
    def test_failure_cleanup_success_and_repeated_shutdown(self):
        source = (ROOT / 'src/app/moonlight_app.cpp').read_text()
        preamble = 'typedef int bool; typedef struct { void *memory; int size, flags; } SceNetInitParam;'
        bodies = {name: c_parser.CParser().parse(preamble + strip_directives(function(source, name)))
                  .ext[-1].body for name in ('InitializeNetwork', 'ShutdownNetwork')}
        stages = ['load', 'allocate', 'net_init', 'ctl_init']
        for failure in (None, *stages):
            with self.subTest(failure=failure):
                calls = []
                values = dict(s_net_initialized=0, s_netctl_initialized=0,
                              s_net_module_loaded=0, s_net_memory=0,
                              false=0, true=1, NULL=0, SCE_SYSMODULE_NET=1, kNetMemorySize=1048576)

                def call(stage, success):
                    calls.append(stage)
                    return (0 if stage == 'allocate' else -10) if stage == failure else success

                values.update(
                    sceSysmoduleLoadModule=lambda module: call('load', 0),
                    sce_paf_malloc=lambda size: call('allocate', 99),
                    sceNetInit=lambda param: call('net_init', 0),
                    sceNetCtlInit=lambda: call('ctl_init', 0),
                    sceNetCtlTerm=lambda: calls.append('ctl_term'),
                    sceNetTerm=lambda: calls.append('net_term'),
                    sce_paf_free=lambda pointer: calls.append(('free', pointer)),
                    sceSysmoduleUnloadModule=lambda module: calls.append('unload'),
                    sce_paf_memset=lambda pointer, *args: values.update({pointer: {}}))
                values['ShutdownNetwork'] = lambda: run(bodies['ShutdownNetwork'], values)
                result = run(bodies['InitializeNetwork'], values)
                if failure:
                    expected = stages[:stages.index(failure) + 1]
                    if failure == 'ctl_init':
                        expected += ['net_term']
                    if failure in ('net_init', 'ctl_init'):
                        expected += [('free', 99)]
                    if failure != 'load':
                        expected += ['unload']
                    self.assertEqual(result, -1 if failure == 'allocate' else -10)
                    self.assertEqual(calls, expected)
                else:
                    self.assertEqual(result, 0)
                    self.assertEqual(run(bodies['InitializeNetwork'], values), 0)
                    self.assertEqual(calls, stages)
                    run(bodies['ShutdownNetwork'], values)
                    self.assertEqual(calls, stages + ['ctl_term', 'net_term', ('free', 99), 'unload'])
                previous = list(calls)
                run(bodies['ShutdownNetwork'], values)
                self.assertEqual(calls, previous)
                for field in ('s_net_initialized', 's_netctl_initialized',
                              's_net_module_loaded', 's_net_memory'):
                    self.assertFalse(values[field])


if __name__ == '__main__':
    unittest.main()
