"""Source checks for the LiveArea text probe. Not a Vita or shell test."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ElementTree

ROOT = Path(__file__).resolve().parents[1]


def template_format():
    source = (ROOT / 'tests/runtime/livearea_probe.cpp').read_text()
    marker = '"</livearea>\\n";'
    head = source.split('const char kTemplate[] =', 1)[1].split(marker, 1)[0]
    body = head + '"</livearea>\\n"'
    parts = re.findall(r'"((?:\\.|[^"\\])*)"', body)
    text = ''.join(part.replace('\\n', '\n').replace('\\"', '"') for part in parts)
    specifiers = re.findall(r'%(?:%|d)', text)
    if specifiers.count('%d') != 11 or len(specifiers) != 11:
        raise AssertionError('template specifiers: %r' % specifiers)
    return text


class LiveAreaProbe(unittest.TestCase):
    def test_revision_template_is_well_formed_text_only(self):
        xml = template_format() % ((7,) * 11)
        root = ElementTree.fromstring(xml)
        self.assertEqual(root.attrib['style'], 'a5')
        self.assertEqual(root.attrib['content-rev'], '7')
        frames = {frame.attrib['id']: frame for frame in root.findall('frame')}
        self.assertEqual(list(frames), ['frame%d' % index for index in range(1, 8)])
        for frame in frames.values():
            self.assertEqual(frame.attrib['rev'], '7')
        self.assertEqual(frames['frame1'].find('liveitem/target').text,
                         'psla:eboot?probe=1&rev=7')
        self.assertEqual(frames['frame1'].find('liveitem/text/str').text, 'Test App 7')
        self.assertEqual(frames['frame7'].find('liveitem/text/str').text, 'Test PC 7')
        self.assertEqual(frames['frame7'].find('liveitem/target').text, 'psla:eboot')
        for index in range(2, 7):
            self.assertIsNone(frames['frame%d' % index].find('liveitem/target'))
            self.assertIsNone(frames['frame%d' % index].find('liveitem/text'))

    def test_probe_is_opt_in_and_records_the_event_before_publish(self):
        source = (ROOT / 'tests/runtime/livearea_probe.cpp').read_text()
        call = source.split('const int length = snprintf(xml, xml_size, kTemplate,', 1)[1]
        arguments = call.split(');', 1)[0]
        self.assertEqual(arguments.count('rev'), 11)
        run = source.split('void livearea_probe_run(void)', 1)[1]
        self.assertLess(run.index('log_launch_event();'), run.index('next_revision();'))
        self.assertLess(run.index('prepare_payload('), run.index('run_promoter();'))
        self.assertIn('event.type == 0', source)
        self.assertIn('sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL)', source)
        self.assertIn('sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL)', source)
        self.assertIn('fallback skipped; promoter reported success', source)
        self.assertIn('ux0:app/VLMP00001/sce_sys/livearea/contents/template.xml', source)
        self.assertIn('ux0:appmeta/VLMP00001/livearea/contents/template.xml', source)

        app = (ROOT / 'src/app/moonlight_app.cpp').read_text()
        self.assertIn('#ifdef MOONLIGHT_LIVEAREA_PROBE\n    livearea_probe_run();\n#endif', app)
        self.assertLess(app.index('sceAppUtilInit(&init, &boot);'), app.index('livearea_probe_run();'))

        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('option(MOONLIGHT_LIVEAREA_PROBE ', cmake)
        self.assertIn('tests/runtime/livearea_probe.cpp', cmake)
        self.assertIn('ScePromoterUtil_stub', cmake)
        script = (ROOT / 'build.sh').read_text().split('local -a profile_args=', 1)[1].split('"${CMAKE_ARGS[@]}"', 1)[0]
        self.assertIn('-DMOONLIGHT_LIVEAREA_PROBE=OFF', script)


if __name__ == '__main__':
    unittest.main()
