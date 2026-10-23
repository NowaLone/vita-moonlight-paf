"""Interpret real discovery worker/control/publication with serial clock/kernel/socket mocks.

No compilation, real pthread scheduling, socket latency or Vita behavior is established.
"""
import ast
import copy
import re
import unittest
from unittest.mock import patch
from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_host_lists import ContinueLoop, function, host
from test_xml_source import ROOT, strip_directives


class Scope:
    def __init__(self, shared, parameters): self.shared, self.local = shared, parameters
    def __getitem__(self, key): return self.local[key] if key in self.local else self.shared[key]
    def __setitem__(self, key, value): (self.shared if key.startswith('s_') else self.local)[key] = value
    def __contains__(self, key): return key in self.local or key in self.shared


def empty_host():
    entry = host(0, host_id='\0', name='\0', internal='\0', external='\0')
    entry.update(id=0, paired=0, online=0, prefer_external=0, port=0)
    for key in ('host_id', 'name', 'internal', 'external', 'mac'):
        entry[key] = [0] * (256 if key not in ('host_id', 'mac') else 33 if key == 'host_id' else 18)
    return entry


def snapshot(): return dict(generation=0, revision=0, count=0, hosts=[empty_host() for _ in range(16)])


class NativeModel:
    def __init__(self, functions, constants):
        self.functions = functions
        self.now, self.end_at, self.latency, self.query_result, self.poll_result = 1, 12000001, 0, 0, 0
        self.create_result, self.attr_result, self.stack_result = 0, 0, 0
        self.pthread_worker = False
        self.renew = True
        self.calls, self.queries, self.logs = [], [], []
        self.on_poll = lambda: None
        self.base_expression, self.base_execute = runtime.expression, runtime.execute
        self.values = dict(constants, NULL=0, s_snapshot=snapshot(), s_seen=[0]*16,
                           s_mutex=0, s_wake=0, s_thread=0, s_thread_created=0, s_enabled=0, s_shutdown=0,
                           s_generation=0, s_active_generation=0, s_lease_until=0,
                           s_healthy_time=0, s_last_error=0, discovery_thread=0,
                           sceKernelGetProcessTimeWide=lambda: self.now,
                           pthread_attr_init=lambda *args: self.record('attr_init', self.attr_result),
                           pthread_attr_setstacksize=self.set_stack,
                           pthread_attr_destroy=lambda *args: self.record('attr_destroy', 0),
                           pthread_create=self.create,
                           pthread_join=lambda *args: self.record('join', 0),
                           sceKernelDelayThread=self.delay,
                           udp_sniffer_vita_init=lambda: self.record('open', 0),
                           udp_sniffer_vita_deinit=lambda: self.record('close', 0),
                           udp_sniffer_vita_set_callback=lambda *args: None,
                           udp_sniffer_vita_query=self.query, udp_sniffer_vita_poll=self.poll,
                           pthread_mutex_lock=lambda *args: None, pthread_mutex_unlock=lambda *args: None,
                           pthread_cond_broadcast=lambda *args: None, pthread_cond_wait=self.wait,
                           vita_debug_log=lambda *args: self.logs.append(args), memset=self.memset,
                           strncpy=self.strncpy, strcmp=lambda a,b: 0 if self.text(a)==self.text(b) else 1,
                           strncmp=lambda a,b,n: 0 if self.text(a)[:n]==self.text(b)[:n] else 1,
                           set_generation=lambda value: self.record('generation', value),
                           shift_hosts=lambda i: self.shift('hosts', i), shift_seen=lambda i: self.shift('seen', i))
        for name in functions:
            self.values[name] = lambda *args, name=name: self.invoke(name, *args)

    def record(self, name, result): self.calls.append(name); return result
    @staticmethod
    def text(value): return value if isinstance(value, str) else bytes(value[:value.index(0)]).decode()
    @staticmethod
    def strncpy(destination, source, count):
        data = list((NativeModel.text(source)).encode())
        destination[:count] = (data + [0]*count)[:count]
    @staticmethod
    def memset(destination, value, count):
        fresh = snapshot() if 'hosts' in destination else empty_host()
        destination.clear(); destination.update(fresh)
    def shift(self, kind, index):
        entries = self.values['s_snapshot']['hosts'] if kind == 'hosts' else self.values['s_seen']
        count = self.values['s_snapshot']['count'] - index
        entries[index:index+count] = copy.deepcopy(entries[index+1:index+1+count])
    def set_stack(self, attr, size):
        assert size == 65536
        return self.record('stack', self.stack_result)
    def create(self, *args):
        if self.create_result == 0: self.pthread_worker = True
        return self.record('create', self.create_result)
    def query(self):
        self.queries.append(self.now)
        self.now += self.latency
        return self.query_result
    def poll(self): self.on_poll(); return self.poll_result
    def delay(self, duration):
        self.now += duration
        if self.now >= self.end_at: self.invoke('legacy_host_discovery_stop')
        elif self.renew: self.invoke('legacy_host_discovery_start', 0)
    def wait(self, *args):
        assert self.pthread_worker, 'cancellable pthread wait on a foreign kernel thread'
        self.calls.append('wait')
        self.values['s_shutdown'] = 1  # Stop the serial worker at its inactive wait point.
        self.values['s_active_generation'] = 0
    def assign(self, node, value, values):
        if isinstance(node, c_ast.ID): values[node.name] = value
        elif isinstance(node, c_ast.StructRef): self.expression(node.name, values)[node.field.name] = value
        elif isinstance(node, c_ast.ArrayRef): self.expression(node.name, values)[self.expression(node.subscript, values)] = value
        elif isinstance(node, c_ast.UnaryOp) and node.op == '*':
            destination = self.expression(node.expr, values)
            destination.clear(); destination.update(copy.deepcopy(value))
        else: raise AssertionError(node)
    def expression(self, node, values):
        if isinstance(node, c_ast.Constant):
            if node.type == 'string': return ast.literal_eval(node.value)
            if node.type == 'char': return ord(ast.literal_eval(node.value))
            return int(re.sub('[uUlL]+$', '', node.value), 0)
        if isinstance(node, c_ast.UnaryOp):
            if node.op == 'sizeof':
                if isinstance(node.expr, c_ast.Typename): return 1
                operand = self.expression(node.expr, values)
                return len(operand) if isinstance(operand, list) else 1
            if node.op == '&': return self.expression(node.expr, values)
            if node.op == '*': return self.expression(node.expr, values)
            if node.op in ('++','--'):
                value = self.expression(node.expr, values) + (1 if node.op=='++' else -1)
                self.assign(node.expr, value, values); return value
        if isinstance(node, c_ast.BinaryOp) and node.op in ('<=','*'):
            a,b=self.expression(node.left, values),self.expression(node.right, values)
            return a<=b if node.op=='<=' else a*b
        return self.base_expression(node, values)
    def execute(self, node, values):
        if isinstance(node, c_ast.Assignment):
            result = self.expression(node.rvalue, values)
            if node.op == '+=': result += self.expression(node.lvalue, values)
            elif node.op != '=': raise AssertionError(node.op)
            self.assign(node.lvalue, result, values)
        elif isinstance(node, c_ast.Continue): raise ContinueLoop()
        elif isinstance(node, (c_ast.For, c_ast.While)):
            if isinstance(node, c_ast.For): self.execute(node.init, values)
            steps=0
            while node.cond is None or self.expression(node.cond, values):
                steps+=1
                assert steps < 10000
                try: self.execute(node.stmt, values)
                except ContinueLoop: pass
                except runtime.BreakSwitch: break
                if isinstance(node, c_ast.For): self.execute(node.next, values)
        elif isinstance(node, (c_ast.UnaryOp, c_ast.Cast)): self.expression(node, values)
        elif isinstance(node, c_ast.DeclList):
            for declaration in node.decls: self.execute(declaration, values)
        else: self.base_execute(node, values)
    def invoke(self, name, *args):
        fn=self.functions[name]
        parameters=fn.decl.type.args.params if fn.decl.type.args else []
        values=Scope(self.values, dict(zip((p.name for p in parameters), args)))
        with patch.object(runtime, 'expression', self.expression), patch.object(runtime, 'execute', self.execute):
            try: self.execute(fn.body, values)
            except runtime.Returned as result: return result.value


class DiscoveryLifecycle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.native=(ROOT/'src/backend/legacy/legacy_host_discovery.c').read_text()
        source=strip_directives(cls.native)
        source=source[source.index('static void report_error'):]
        source=source.replace('*generation = s_generation;', 'set_generation(s_generation);')
        source=re.sub(r'memmove\(&s_snapshot.hosts\[i\].*?;', 'shift_hosts(i);', source, flags=re.S)
        source=re.sub(r'memmove\(&s_seen\[i\].*?;', 'shift_seen(i);', source, flags=re.S)
        preamble='''typedef int pthread_attr_t; typedef unsigned int uint32_t; typedef unsigned short uint16_t;
        typedef unsigned long long uint64_t;
        typedef struct { int id; char host_id[33], name[256], internal[256], external[256], mac[18];
        int port,paired,online,prefer_external;} MoonlightHost;
        typedef struct {uint32_t generation,revision; int count; MoonlightHost hosts[16];} MoonlightDiscoverySnapshot;'''
        service = (ROOT/'src/services/host_discovery_service.cpp').read_text()
        accepts = function(service, 'HostDiscoveryService::Accepts')
        accepts = accepts.replace('HostDiscoveryService::Accepts', 'accepts').replace(
            'const MoonlightEvent &event', 'const MoonlightEvent *event').replace(' const\n', '\n')
        preamble += 'typedef int bool; typedef struct {uint32_t discovery_generation;} MoonlightEvent;'
        unit=c_parser.CParser().parse(preamble+source+accepts)
        cls.functions={item.decl.name:item for item in unit.ext if isinstance(item,c_ast.FuncDef)}
        cls.constants={name:int(re.sub('[uUlL]+$','',value),0)
                       for name,value in re.findall(r'^#define (DISCOVERY_\w+) (\w+)',cls.native,re.M)}
        cls.constants['MOONLIGHT_MAX_DISCOVERED_HOSTS']=16

    def setUp(self): self.model=NativeModel(self.functions,self.constants)

    def test_five_second_queries_in_one_worker_no_catch_up(self):
        m=self.model; m.latency=80000
        self.assertEqual(m.invoke('legacy_host_discovery_start',0),0)
        m.invoke('discovery_thread',0)
        self.assertEqual(m.queries,[1,5080001,10160001])
        self.assertEqual(m.calls.count('create'),1)
        self.assertNotIn('join',m.calls)
        self.assertEqual(m.calls.count('close'),1)

    def test_stop_restart_preserves_worker_and_rejects_old_observation(self):
        m=self.model; m.invoke('legacy_host_discovery_start',0)
        generation=m.values['s_generation'];m.values['s_active_generation']=generation
        m.invoke('on_mdns_host',0,'service','PC','192.0.2.1',47989)
        m.invoke('legacy_host_discovery_stop')
        m.invoke('legacy_host_discovery_start',0)
        self.assertGreater(m.values['s_generation'],generation)
        m.invoke('on_mdns_host',0,'service','Old result','192.0.2.2',47989)
        self.assertEqual(m.values['s_snapshot']['count'],1)
        self.assertNotIn('join',m.calls)
        self.assertEqual(m.calls.count('create'),1)

    def test_snapshot_is_value_copy_unchanged_read_is_noop_and_cancel_rejects(self):
        m=self.model;m.invoke('legacy_host_discovery_start',0)
        m.values['s_active_generation']=m.values['s_generation']
        m.invoke('on_mdns_host',0,'service','PC','192.0.2.1',47989)
        out=snapshot()
        self.assertEqual(m.invoke('legacy_host_discovery_read',out),1)
        m.values['s_snapshot']['hosts'][0]['name'][0]=ord('X')
        self.assertNotEqual(out['hosts'][0]['name'],m.values['s_snapshot']['hosts'][0]['name'])
        self.assertEqual(m.invoke('legacy_host_discovery_read',out),0)
        m.invoke('legacy_host_discovery_stop')
        self.assertEqual(m.invoke('legacy_host_discovery_read',out),-1)

    def test_last_observation_refreshes_expiry_and_name_does_not_deduplicate(self):
        m=self.model;m.invoke('legacy_host_discovery_start',0)
        m.values['s_active_generation']=m.values['s_generation']
        for ip in ('192.0.2.1','192.0.2.2'):
            m.invoke('on_mdns_host',0,'service','Same name',ip,47989)
        m.values['s_healthy_time']=10000000
        m.invoke('on_mdns_host',0,'service','Renamed','192.0.2.1',47989)
        m.values['s_healthy_time']=14999999;m.invoke('expire_hosts')
        self.assertEqual(m.values['s_snapshot']['count'],2)
        m.values['s_healthy_time']=15000000;m.invoke('expire_hosts')
        self.assertEqual(m.values['s_snapshot']['count'],1)
        self.assertEqual(m.text(m.values['s_snapshot']['hosts'][0]['name']),'Renamed')
        m.values['s_healthy_time']=25000000;m.invoke('expire_hosts')
        self.assertEqual(m.values['s_snapshot']['count'],0)

    def test_failed_network_freezes_age_and_deduplicates_log(self):
        m=self.model;m.query_result=-15;m.invoke('legacy_host_discovery_start',0)
        m.invoke('discovery_thread',0)
        self.assertEqual(m.values['s_healthy_time'],0)
        self.assertEqual(len(m.logs),1)
        self.assertEqual(len(m.queries),3)

    def test_unrenewed_ui_lease_closes_socket_without_waiting_on_ui(self):
        m=self.model;m.renew=False;m.invoke('legacy_host_discovery_start',0)
        m.invoke('discovery_thread',0)
        self.assertLessEqual(m.now,260001)
        self.assertEqual(m.calls.count('close'),1)
        self.assertNotIn('join',m.calls)
        self.assertTrue(m.invoke('legacy_host_discovery_is_idle'))

    def test_pthread_setup_errors_cleanup_retry_and_final_shutdown_joins(self):
        m=self.model
        m.attr_result=12
        self.assertEqual(m.invoke('legacy_host_discovery_start',0),-12)
        self.assertEqual(m.calls,['attr_init'])
        self.assertFalse(m.values['s_thread_created'])
        m.attr_result=0;m.stack_result=22;m.calls.clear()
        self.assertEqual(m.invoke('legacy_host_discovery_start',0),-22)
        self.assertEqual(m.calls,['attr_init','stack','attr_destroy'])
        m.stack_result=0;m.create_result=11;m.calls.clear()
        self.assertEqual(m.invoke('legacy_host_discovery_start',0),-11)
        self.assertEqual(m.calls,['attr_init','stack','create','attr_destroy'])
        self.assertFalse(m.values['s_thread_created'])
        m.create_result=0;m.invoke('legacy_host_discovery_start',0)
        self.assertTrue(m.values['s_thread_created'])
        m.invoke('legacy_host_discovery_shutdown')
        self.assertEqual(m.calls[-1],'join')
        self.assertFalse(m.values['s_thread_created'])

    def test_connect_barrier_has_no_cancellable_wait_on_a_paf_thread(self):
        m=self.model;m.values['s_active_generation']=1
        def release(duration):
            self.assertEqual(duration,10000)
            m.values['s_active_generation']=0
            m.calls.append('release')
        m.values['sceKernelDelayThread']=release
        m.invoke('legacy_host_discovery_wait_idle')
        self.assertEqual(m.calls,['release'])
        self.assertFalse(m.pthread_worker)
        self.assertNotIn('sceKernelCreateThread',self.native)
        self.assertIn('pthread_create(&s_thread, &attr, discovery_thread, NULL)',self.native)

    def test_service_generation_acceptance_requires_current_active_search(self):
        m=self.model
        for active, delivered, expected in ((0,0,False),(0,1,False),(2,1,False),(2,2,True)):
            m.values['m_generation']=active
            self.assertEqual(bool(m.invoke('accepts',dict(discovery_generation=delivered))),expected)

    def test_only_connect_worker_waits_ui_uses_idle_poll_and_app_dispatch(self):
        connection=(ROOT/'src/services/connection_service.cpp').read_text()
        run=connection.split('virtual void Run()',1)[1].split('virtual void Finish()',1)[0]
        self.assertLess(run.index('WaitForHostSearchIdle'),run.index('ConnectHost'))
        main=(ROOT/'src/pages/page_main.cpp').read_text()
        self.assertNotIn('WaitForHostSearchIdle',main)
        self.assertIn('app->Discovery().IsIdle()',main)
        self.assertIn('app->Discovery().Start()',main)
        self.assertIn('m_system_ui_overlaid',main)
        stop=function(self.native,'legacy_host_discovery_stop')
        self.assertNotIn('Wait',stop)
        self.assertNotIn('Join',stop)
        app=(ROOT/'src/app/moonlight_app.cpp').read_text()
        self.assertIn('!app->m_discovery.Accepts(event)',app)
        self.assertIn('page::Base::GetCurrent() != page::Main::Instance()',app)
        # Worker never sends callbacks to whichever page currently owns the app callback.
        self.assertNotIn('s_callback',self.native)


if __name__=='__main__': unittest.main()
