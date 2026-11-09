"""Cache key and corrupt-record contracts independent of a native compiler."""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from shader_cache import cache_key, cache_read, cache_write


class ShaderCacheTests(unittest.TestCase):
    def test_all_key_inputs_invalidate(self):
        args = ['source', 'expanded', ['-E', 'entry', '-T', 'ps_6_0'], {'dxc': 'pin'}, 'DXIL']
        key = cache_key(*args)
        for index, change in enumerate(('source edit', 'include edit', ['-E', 'other'], {'dxc': 'new'}, 'SPIR-V')):
            with self.subTest(index=index):
                modified = args.copy()
                modified[index] = change
                self.assertNotEqual(cache_key(*modified), key)

    def test_record_rejects_wrong_key_and_modified_payload(self):
        key = b'a'*32
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)/'entry'
            self.assertEqual(cache_read(path, key), ('miss', None))
            cache_write(path, key, b'object')
            self.assertEqual(cache_read(path, key), ('hit', b'object'))
            self.assertEqual(cache_read(path, b'b'*32), ('repaired', None))
            data = path.read_bytes()
            path.write_bytes(data[:-1]+b'x')
            self.assertEqual(cache_read(path, key), ('repaired', None))
            cache_write(path, key, b'new object')
            self.assertEqual(cache_read(path, key), ('hit', b'new object'))


if __name__ == '__main__':
    unittest.main()
