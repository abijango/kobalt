"""Check the fixed UI inventory and report its observed acceptance results."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
INVENTORY = ROOT / 'tests/native-ui/correctness_checklist.json'
SOURCES = {'reader-window': 'tests/native-ui/reader_test.cpp',
           'reader-window-device': 'tests/native-ui/reader_test.cpp',
           'desktop-window': 'tests/desktop/window_test.cpp'}
SHARDS = {'reader-window': {f'reader-window-{index}' for index in range(12)},
          'desktop-window': {f'desktop-window-{index}' for index in range(8)} |
                            {f'desktop-window-books-{index}' for index in range(5)}}


def inventory():
    return json.loads(INVENTORY.read_text())


def cases():
    return inventory()['cases']


def required_scenes(group):
    frozen = inventory()['required_captures'][group]
    return {(group, scene, theme, ppi)
            for ppi in (300, 212) for theme in ('light', 'dark')
            for scene in frozen['scenes'] + frozen.get('density_scenes', {}).get(str(ppi), [])}


def scene_identity(shot):
    return (shot['group'], shot['name'].rsplit('/', 1)[-1], shot['theme'], shot.get('ppi', 300))


def native_results(paths):
    results = {}
    for path in paths:
        for test in ET.parse(path).iter('testcase'):
            passed = not any(test.find(tag) is not None for tag in ('failure', 'error', 'skipped'))
            results[test.attrib['name']] = (passed, test.findtext('system-out', ''))
    # The checklist names complete executables. All their registered groups
    # must be present and pass before their combined slot evidence can count.
    for name, shards in SHARDS.items():
        observed = [results[shard] for shard in sorted(shards) if shard in results]
        if observed:
            results[name] = (len(observed) == len(shards) and all(passed for passed, _ in observed),
                             '\n'.join(output for _, output in observed))
    return results


def report(results, manifest, gates):
    binary = gates.get('visual-parity', {}).get('native_binary_sha256')
    shots = manifest.get('screens', [])
    counts = Counter(scene_identity(shot) for shot in shots)
    screens = set(counts)
    outcomes = []
    for case in cases():
        missing, failed, evidence = [], [], []
        for assertion in case.get('assertions', []):
            test, _, slot = assertion.partition(':')
            observed = results.get(test)
            if observed is None:
                missing.append('No current test result for ' + assertion)
            elif not observed[0]:
                failed.append('Test failed: ' + test)
            elif slot and not re.search(r'PASS\s*:\s*\w+::' + re.escape(slot) + r'\(', observed[1]):
                missing.append('No passing slot evidence for ' + assertion)
            else:
                evidence.append(assertion)
        for group in case.get('captures', []):
            expected = required_scenes(group)
            absent = sorted(expected - screens)
            duplicates = sorted(identity for identity, count in counts.items() if identity[0] == group and count > 1)
            if not manifest.get('complete'):
                missing.append('Capture manifest is incomplete for ' + group)
            if absent:
                missing.append('Missing capture scenes: ' + ', '.join('/'.join(map(str, identity)) for identity in absent))
            if duplicates:
                failed.append('Duplicate capture scenes: ' + ', '.join('/'.join(map(str, identity)) for identity in duplicates))
            current = binary and manifest.get('binary_sha256') == binary and all(
                shot.get('binary_sha256') == binary for shot in shots if scene_identity(shot) in expected)
            if not current:
                missing.append('Captures do not identify the current native binary for ' + group)
            if manifest.get('complete') and not absent and not duplicates and current:
                evidence.append('captures: ' + group)
        if 'gate' in case:
            gate = gates.get(case['gate'])
            if not gate:
                missing.append('No assigned platform result for ' + case['gate'])
            elif gate.get('exit_code') != 0:
                failed.append('Platform command failed: ' + case['gate'])
            else:
                log = Path(gate.get('log', ''))
                if not log.is_file():
                    missing.append('Platform log is missing: ' + case['gate'])
                else:
                    text = log.read_text()
                    required = gate.get('required_lines', [])
                    if not text.strip() or not required or any(line not in text for line in required):
                        missing.append('Platform log lacks required evidence: ' + case['gate'])
                    else:
                        evidence.append(str(log))
        outcomes.append(dict(id=case['id'], platform=case['platform'], expected=case['expected'],
                             status='failed' if failed else 'blocked' if missing else 'passed',
                             reasons=failed + missing, evidence=evidence))
    return outcomes


class CorrectnessInventoryTests(unittest.TestCase):
    def shard_results(self, missing=None, failed=None):
        document = ET.Element('testsuite')
        for name, shards in SHARDS.items():
            for shard in sorted(shards):
                if shard == missing:
                    continue
                test = ET.SubElement(document, 'testcase', name=shard)
                ET.SubElement(test, 'system-out').text = 'PASS : ReaderTest::' + shard + '()'
                if shard == failed:
                    ET.SubElement(test, 'failure')
        device = ET.SubElement(document, 'testcase', name='reader-window-device')
        ET.SubElement(device, 'system-out').text = 'device evidence'
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'native.xml'
            ET.ElementTree(document).write(path)
            return native_results([path])

    def test_junit_combines_all_groups_and_keeps_device_evidence_separate(self):
        results = self.shard_results()
        for name, shards in SHARDS.items():
            self.assertTrue(results[name][0])
            for shard in shards:
                self.assertIn(shard + '()', results[name][1])
        self.assertEqual(results['reader-window-device'], (True, 'device evidence'))

    def test_missing_or_failed_group_cannot_pass_complete_executable(self):
        for name, shards in SHARDS.items():
            shard = sorted(shards)[-1]
            self.assertFalse(self.shard_results(missing=shard)[name][0])
            self.assertFalse(self.shard_results(failed=shard)[name][0])

    def test_evidence_groups_match_ctest_registration(self):
        cmake = (ROOT / 'tests/CMakeLists.txt').read_text()
        groups = dict(re.findall(r'foreach\(shard RANGE 0 (\d+)\)(.*?)endforeach\(\)', cmake, re.DOTALL))
        for prefix, count in (('reader-window', 12), ('desktop-window', 8), ('desktop-window-books', 5)):
            self.assertIn('add_test(NAME ' + prefix + '-${shard}', groups[str(count - 1)])

    def capture_fixture(self):
        shots = [dict(group=group, name=scene, theme=theme, ppi=ppi, binary_sha256='current')
                 for group in inventory()['required_captures']
                 for _, scene, theme, ppi in sorted(required_scenes(group))]
        return dict(complete=True, binary_sha256='current', screens=shots)

    def observed_assertions(self):
        results = {}
        for case in cases():
            for assertion in case.get('assertions', []):
                test, _, slot = assertion.partition(':')
                prior = results.get(test, (True, ''))[1]
                results[test] = (True, prior + ('PASS : ReaderTest::' + slot + '()\n' if slot else ''))
        return results

    def capture_outcomes(self, manifest):
        return {item['id']: item for item in report(self.observed_assertions(), manifest,
                {'visual-parity': {'native_binary_sha256': 'current'}})}

    def test_each_required_scene_is_checked_when_group_remains(self):
        for group, scene, case in [('dictionary', 'definition-page-2', 'dictionary-lookup'),
                                   ('about', 'license', 'stats-long-content'),
                                   ('footnotes', 'return-row', 'footnotes')]:
            with self.subTest(scene=scene):
                manifest = self.capture_fixture()
                self.assertEqual(self.capture_outcomes(manifest)[case]['status'], 'passed')
                manifest['screens'] = [shot for shot in manifest['screens']
                    if not (shot['group'] == group and shot['name'] == scene and shot['theme'] == 'dark' and shot['ppi'] == 212)]
                outcome = self.capture_outcomes(manifest)[case]
                self.assertEqual(outcome['status'], 'blocked')
                self.assertTrue(any(scene in reason for reason in outcome['reasons']))

    def test_chapter_second_page_is_required_only_at_300_ppi(self):
        manifest = self.capture_fixture()
        self.assertFalse(any(shot['group'] == 'chapters' and shot['name'] == 'chapters-02' and shot['ppi'] == 212 for shot in manifest['screens']))
        self.assertEqual(self.capture_outcomes(manifest)['chapters-links']['status'], 'passed')
        manifest['screens'] = [shot for shot in manifest['screens']
            if not (shot['group'] == 'chapters' and shot['name'] == 'chapters-02' and shot['theme'] == 'light')]
        self.assertEqual(self.capture_outcomes(manifest)['chapters-links']['status'], 'blocked')

    def test_duplicate_or_stale_capture_cannot_pass(self):
        manifest = self.capture_fixture()
        dictionary = next(shot for shot in manifest['screens'] if shot['group'] == 'dictionary')
        manifest['screens'].append(dict(dictionary))
        self.assertEqual(self.capture_outcomes(manifest)['dictionary-lookup']['status'], 'failed')
        manifest['screens'].pop()
        dictionary['binary_sha256'] = 'old'
        self.assertEqual(self.capture_outcomes(manifest)['dictionary-lookup']['status'], 'blocked')
        dictionary['binary_sha256'] = 'current'
        manifest['binary_sha256'] = 'old'
        self.assertEqual(self.capture_outcomes(manifest)['dictionary-lookup']['status'], 'blocked')
        manifest['binary_sha256'] = 'current'; manifest['complete'] = False
        self.assertEqual(self.capture_outcomes(manifest)['dictionary-lookup']['status'], 'blocked')

    def test_missing_evidence_cannot_pass_acceptance(self):
        outcomes = report({}, {}, {})
        self.assertTrue(all(item['status'] == 'blocked' and item['reasons'] for item in outcomes))

    def test_passing_slot_text_cannot_hide_failed_executable(self):
        outcomes = report({'reader-window': (False, 'PASS : ReaderTest::canvasInputRespectsModalBarrier()')}, {}, {})
        modal = next(item for item in outcomes if item['id'] == 'modal-input')
        self.assertEqual(modal['status'], 'failed')

    def test_fixed_inventory_has_real_assertions_and_capture_groups(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        from native_screenshots import GROUPS
        inventory = cases()
        self.assertEqual(len(inventory), 30)
        self.assertEqual(len({case['id'] for case in inventory}), len(inventory))
        for case in inventory:
            self.assertTrue(case['expected'])
            self.assertTrue(case.get('assertions') or case.get('captures') or case.get('gate'))
            for assertion in case.get('assertions', []):
                test, _, slot = assertion.partition(':')
                if slot:
                    self.assertIn(test, SOURCES)
                    self.assertRegex((ROOT / SOURCES[test]).read_text(), r'void\s+' + re.escape(slot) + r'\s*\(')
                else:
                    self.assertIn('kobalt_widget_test(' + test + ' ', (ROOT / 'tests/CMakeLists.txt').read_text())
            for group in case.get('captures', []):
                self.assertIn(group, GROUPS)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--junit', type=Path, action='append', required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--gates', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    outcomes = report(native_results(args.junit), json.loads(args.manifest.read_text()),
                      json.loads(args.gates.read_text()))
    args.report.write_text(json.dumps(dict(cases=outcomes), indent=2) + '\n')
    for item in outcomes:
        print(item['status'].upper() + ': ' + item['id'] + ' ' + '; '.join(item['reasons']))
    return int(any(item['status'] != 'passed' for item in outcomes))


if __name__ == '__main__':
    if '--report' in sys.argv:
        raise SystemExit(main())
    unittest.main()
