"""Verify coverage and adapter attribution in Windows GPU smoke receipts."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import windows_release_gpu as smoke


FINGERPRINT = 'a' * 64
SOURCE = f'source={FINGERPRINT} validation=0\n'
D3D12 = ('GraphicsSDK 0.0.1\n' + SOURCE + 'adapter=WARP id=1 software=1\n'
         'Installed resource statistics: 64MiB retain/release verified\n'
         'Installed Depth/MRT consumer: 1024 pixels verified\n')


class WindowsReleaseGPU(unittest.TestCase):
    def test_warp_result_is_not_hardware(self):
        windows_log = D3D12.replace('\n', '\r\n')
        self.assertEqual(smoke.d3d12_result(windows_log, FINGERPRINT, '0.0.1', warp=True)['verified_bytes'], 4096)
        with self.assertRaisesRegex(ValueError, 'adapter kind differs'):
            smoke.d3d12_result(D3D12, FINGERPRINT, '0.0.1', warp=False)

    def test_d3d12_requires_the_workload_and_pinned_fingerprint(self):
        with self.assertRaisesRegex(ValueError, 'did not complete the workload'):
            smoke.d3d12_result(D3D12.replace('1024 pixels', '0 pixels'), FINGERPRINT, '0.0.1', warp=True)
        with self.assertRaisesRegex(ValueError, 'fingerprint or validation mode differs'):
            smoke.d3d12_result(D3D12, 'b' * 64, '0.0.1', warp=True)

    def vulkan_logs(self):
        transfer = SOURCE + 'adapter=GPU id=11 vendor=4318 software=0\n'
        transfer += 'PASS assertions=361 transfers=24 verified_bytes=28521273 adapters=1\n'
        draw = SOURCE + 'adapter=GPU id=11\n'
        counts = smoke.EXPECTED['draw']
        draw += (f'PASS draw_assertions=340 draws=31 images=33 verified_texel_bytes={counts["verified_texel_bytes"]} '
                 f'padded_images={counts["padded_images"]} adapters=1\n')
        return transfer, draw

    def test_vulkan_requires_the_same_hardware_in_both_consumers(self):
        transfer, draw = self.vulkan_logs()
        result = smoke.vulkan_results(transfer, draw, FINGERPRINT)
        self.assertEqual(result['adapters'][0]['id'], '11')
        self.assertEqual(result['draw']['images'], 33)
        with self.assertRaisesRegex(ValueError, 'adapter sets differ or lack hardware'):
            smoke.vulkan_results(transfer, draw.replace('id=11', 'id=12'), FINGERPRINT)
        with self.assertRaisesRegex(ValueError, 'adapter sets differ or lack hardware'):
            smoke.vulkan_results(transfer.replace('software=0', 'software=1'), draw, FINGERPRINT)

    def test_vulkan_rejects_a_truncated_workload(self):
        transfer, draw = self.vulkan_logs()
        with self.assertRaisesRegex(ValueError, 'Vulkan draw workload'):
            smoke.vulkan_results(transfer, draw.replace('images=33', 'images=32'), FINGERPRINT)


if __name__ == '__main__':
    unittest.main()
