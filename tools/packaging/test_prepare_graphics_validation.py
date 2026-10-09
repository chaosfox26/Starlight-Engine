"""A dropped download must not leave a cached file that blocks every later staging run."""
import hashlib
from pathlib import Path
import tempfile
import unittest
import prepare_graphics_validation as module

GOOD = b'complete package'

class FetchVerified(unittest.TestCase):
    def setUp(self):
        self.saved = module.SHA256
        module.SHA256 = hashlib.sha256(GOOD).hexdigest()

    def tearDown(self):
        module.SHA256 = self.saved

    def test_interrupted_download_is_not_kept_and_the_next_run_recovers(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / 'sdk.nupkg'
            def dropped(url, path):
                Path(path).write_bytes(GOOD[:5])
                raise OSError('connection dropped')
            with self.assertRaises(OSError):
                module.fetch_verified(archive, dropped)
            self.assertFalse(archive.exists())
            module.fetch_verified(archive, lambda url, path: Path(path).write_bytes(GOOD))
            self.assertEqual(archive.read_bytes(), GOOD)

    def test_a_bad_cached_file_is_replaced_and_a_good_one_is_not_downloaded_again(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / 'sdk.nupkg'
            archive.write_bytes(GOOD[:5])
            module.fetch_verified(archive, lambda url, path: Path(path).write_bytes(GOOD))
            self.assertEqual(archive.read_bytes(), GOOD)
            module.fetch_verified(archive, lambda url, path: self.fail('downloaded again'))

    def test_a_download_with_the_wrong_hash_is_refused_and_leaves_no_file(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / 'sdk.nupkg'
            with self.assertRaises(RuntimeError):
                module.fetch_verified(archive, lambda url, path: Path(path).write_bytes(b'tampered'))
            self.assertEqual(list(Path(temp).iterdir()), [])

if __name__ == '__main__':
    unittest.main()
