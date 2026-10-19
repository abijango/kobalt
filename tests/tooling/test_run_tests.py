"""Host dependency caches must notice changed inputs and damaged build outputs."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import run_tests


class DependencyCacheTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.output = Path(temporary.name)
        self.library = self.output / 'deps/lib/libexample.a'
        self.library.parent.mkdir(parents=True)
        self.library.write_bytes(b'compiled library')
        self.identity = {'compiler': 'compiler one', 'mode': 'engine'}
        self.options = ['-DKOBALT_TEST_ENGINE=ON']
        run_tests.record_dependencies(self.output, self.identity, self.options)

    def test_complete_unchanged_build_is_reused(self):
        self.assertEqual(run_tests.cached_dependencies(self.output, self.identity), self.options)

    def test_input_change_requires_build(self):
        changed = dict(self.identity, compiler='compiler two')
        self.assertIsNone(run_tests.cached_dependencies(self.output, changed))

    def test_deleted_or_corrupt_library_requires_build(self):
        self.library.write_bytes(b'corrupted library')
        self.assertIsNone(run_tests.cached_dependencies(self.output, self.identity))
        self.library.unlink()
        self.assertIsNone(run_tests.cached_dependencies(self.output, self.identity))

    def test_cache_miss_discards_configured_compilers_and_old_objects(self):
        sources = {name: self.output / ('source-' + name) for name in ('freetype', 'harfbuzz', 'fribidi')}
        builds = [self.output / ('freetype-' + sources['freetype'].name),
                  self.output / ('harfbuzz-' + sources['harfbuzz'].name), self.output / 'fribidi-autotools']
        for directory in builds:
            directory.mkdir()
            (directory / 'CMakeCache.txt').write_text('old compiler flags')
            (directory / 'old-object.o').write_bytes(b'old compiled code')
        def cmake(command, **kwargs):
            if command[:2] == ['cmake', '-S']:
                directory = Path(command[command.index('-B') + 1])
                self.assertFalse(directory.exists(), 'CMake must read the new compiler environment')
                directory.mkdir()
        def autotools(source, directory, *args, **kwargs):
            self.assertFalse(directory.exists(), 'Make must not reuse objects compiled with old flags')
            directory.mkdir()
        with patch.object(run_tests, 'fetch_sources', return_value=sources), \
                patch.object(run_tests, 'dependency_identity', return_value={'text_only': True}), \
                patch.object(run_tests.subprocess, 'run', side_effect=cmake), \
                patch.object(run_tests, 'build_autotools', side_effect=autotools):
            run_tests.build_engine(self.output / 'qt', self.output, 2, text_only=True)
        self.assertTrue(self.library.is_file(), 'Installed outputs stay inside their own prefix')
        self.assertTrue(all(not (directory / 'old-object.o').exists() for directory in builds))

    def test_interrupted_or_invalid_record_requires_build(self):
        record = self.output / 'dependencies.json'
        for content in ('{', '{}', 'null'):
            record.write_text(content)
            self.assertIsNone(run_tests.cached_dependencies(self.output, self.identity))

    def test_record_cannot_read_paths_outside_output(self):
        record = self.output / 'dependencies.json'
        for name in ('../outside', '/outside'):
            record.write_text(json.dumps({'identity': self.identity, 'files': {name: 'hash'}, 'options': []}))
            self.assertIsNone(run_tests.cached_dependencies(self.output, self.identity))

    def test_changed_options_cannot_disable_the_engine_tier(self):
        record = self.output / 'dependencies.json'
        data = json.loads(record.read_text())
        data['options'] = ['-DKOBALT_TEST_ENGINE=OFF']
        record.write_text(json.dumps(data))
        self.assertIsNone(run_tests.cached_dependencies(self.output, self.identity))

    def test_compiler_flags_and_source_checksums_change_identity(self):
        qt = self.output / 'qt'
        specs = {'freetype': {'sha256': 'first source'}}
        with patch.object(run_tests.platform, 'platform', return_value='test host'), \
                patch.object(run_tests.subprocess, 'check_output', return_value='tool version\n'), \
                patch.dict(run_tests.os.environ, {'CXXFLAGS': ''}):
            before = run_tests.dependency_identity(qt, specs, True)
            with patch.dict(run_tests.os.environ, {'CXXFLAGS': '-O0'}):
                flags = run_tests.dependency_identity(qt, specs, True)
            self.assertNotEqual(before, flags)
            changed = copy.deepcopy(specs)
            changed['freetype']['sha256'] = 'second source'
            self.assertNotEqual(before, run_tests.dependency_identity(qt, changed, True))
            alias = ROOT / 'tools/../tools/run_tests.py'
            with patch.object(run_tests, '__file__', str(alias)):
                self.assertEqual(before, run_tests.dependency_identity(qt, specs, True))

    def test_explicit_compiler_version_is_part_of_identity(self):
        with patch.object(run_tests.platform, 'platform', return_value='test host'), \
                patch.object(run_tests.shutil, 'which', side_effect=lambda name: '/tools/' + name), \
                patch.dict(run_tests.os.environ, {'CC': 'custom-cc', 'CXX': 'custom-cxx'}):
            with patch.object(run_tests.subprocess, 'check_output', return_value='first version\n') as version:
                first = run_tests.dependency_identity(self.output, {}, True)
                commands = [call.args[0] for call in version.call_args_list]
                self.assertIn(['/tools/custom-cxx', '--version'], commands)
            with patch.object(run_tests.subprocess, 'check_output', return_value='second version\n'):
                self.assertNotEqual(first, run_tests.dependency_identity(self.output, {}, True))


if __name__ == '__main__':
    unittest.main()
