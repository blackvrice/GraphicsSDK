"""GNU policy evidence contracts; synthetic command graphs execute no compiler."""
import copy
import json
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_build_policy as b
import linux_comparison as q


class ControlledBuild(unittest.TestCase):
    def fixture(self,root):
        builds={name:root/name for name in ('sdk','sdl','probe')}
        for path in builds.values():
            path.mkdir();(path/'CMakeCache.txt').write_text(''.join(k+':STRING='+v+'\n' for k,v in b.SETTINGS.items()),encoding='utf-8')
            (path/'compile_commands.json').write_text(json.dumps([dict(command='g++-14 -O2 -DNDEBUG -c example.cpp -o example.o')]),encoding='utf-8')
        out=root/'evidence';out.mkdir();value=b.capture(out,builds,q.record)
        frozen=[q.record(path/name) for path in builds.values() for name in ('CMakeCache.txt','compile_commands.json')]
        return builds,out,value,frozen

    def test_capture_matches_raw_cache_and_compile_graph(self):
        with tempfile.TemporaryDirectory() as td:
            builds,out,value,frozen=self.fixture(pathlib.Path(td))
            b.check(out,value,builds,frozen)
            self.assertEqual(value['name'],b.NAME);self.assertEqual(set(value['projects']),set(builds))
            self.assertIn('-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG',b.ARGS)

    def test_compile_flags_reject_other_optimization_lto_and_tuning(self):
        good='g++-14 -O2 -DNDEBUG -c a.cpp -o a.o';b.flags([dict(command=good)])
        for flag in ('-O0','-O1','-O3','-Ofast','-Os','-Og','-Oz','-flto','-flto=auto','-march=native','-mtune=native'):
            with self.subTest(flag=flag),self.assertRaises(ValueError):b.flags([dict(command=good+' '+flag)])
        for command in (good.replace('-O2',''),good.replace('-DNDEBUG','')):
            with self.assertRaises(ValueError):b.flags([dict(command=command)])

    def test_hash_and_frozen_cache_bindings_cannot_be_dropped(self):
        with tempfile.TemporaryDirectory() as td:
            builds,out,value,frozen=self.fixture(pathlib.Path(td))
            with self.assertRaises(ValueError):b.check(out,value,builds,frozen[:-1])
            (out/'controlled/sdk-commands.json').write_text('[]',encoding='utf-8')
            with self.assertRaises(ValueError):b.check(out,value,builds,frozen)

    def test_rebound_hashes_do_not_hide_uncontrolled_release_flags(self):
        for name in ('cache','commands'):
            with self.subTest(name=name),tempfile.TemporaryDirectory() as td:
                builds,out,value,frozen=self.fixture(pathlib.Path(td))
                file='CMakeCache.txt' if name=='cache' else 'compile_commands.json'
                source=builds['sdk']/file;source.write_text(source.read_text(encoding='utf-8').replace('-O2','-O3'),encoding='utf-8')
                dest=out/'controlled'/('sdk-cache.txt' if name=='cache' else 'sdk-commands.json');dest.write_bytes(source.read_bytes())
                old=value['projects']['sdk'][name];value['projects']['sdk'][name]=q.record(source)
                frozen=[value['projects']['sdk'][name] if row==old else row for row in frozen]
                with self.assertRaises(ValueError):b.check(out,value,builds,frozen)

    def test_controlled_profile_and_project_set_are_exact(self):
        with tempfile.TemporaryDirectory() as td:
            builds,out,value,frozen=self.fixture(pathlib.Path(td))
            bad=copy.deepcopy(value);bad['name']='stock-release'
            with self.assertRaises(ValueError):b.check(out,bad,builds,frozen)
            with self.assertRaises(ValueError):b.scope(value,('sdk','sdl'))
            with self.assertRaises(ValueError):b.scope(None,builds)


if __name__=='__main__':unittest.main()
