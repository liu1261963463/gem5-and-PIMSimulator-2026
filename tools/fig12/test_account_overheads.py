import unittest
from account_overheads import account


def measured_case():
    # Actual uploaded M=768,K=2048 measurement in commit d3981fc.
    return dict(backend='HBM2_diagnostic_not_paper_DDR5', correct=True,
                M=768, K=2048, M_padded=1024, K_padded=2048, tCK_ns=1,
                weight_preload_cycles=22109, kernel_including_upload_cycles=6951,
                result_read_cycles=181, host_reduction_timed=False)


class AccountingTest(unittest.TestCase):
    def test_no_double_count_and_resident_cold_distinction(self):
        result = account(measured_case(), 5.0, 100.0)
        self.assertEqual(result['backend_measured_ns'], 7132)
        self.assertEqual(result['additional_replication_ns'], 0)
        self.assertEqual(result['host_reduction_estimated_ns'], 5220)
        self.assertEqual(result['resident_weights_estimated_ns'], 12352)
        self.assertEqual(result['cold_weights_estimated_ns'], 34461)
        # A 4096-byte vector uploaded once to each of 16 channels.
        # Do not multiply by the eight internal PIM blocks again.
        self.assertEqual(result['derived_input_upload_bytes'], 65536)
        self.assertEqual(result['derived_result_read_bytes'], 32768)
        self.assertEqual(result['source_level_fp32_additions'], 16384)

    def test_host_cost_applies_to_padded_rows(self):
        low = account(measured_case(), 1, 0)
        high = account(measured_case(), 2, 0)
        self.assertEqual(high['resident_weights_estimated_ns'] -
                         low['resident_weights_estimated_ns'], 1024)
        self.assertLess(high['resident_weights_estimated_gflops'],
                        low['resident_weights_estimated_gflops'])

    def test_reject_invalid_host_parameters(self):
        for value in (0, -1, float('nan'), float('inf')):
            with self.subTest(value=value), self.assertRaises(ValueError):
                account(measured_case(), value, 0)
        for value in (-1, float('nan'), float('inf')):
            with self.subTest(setup=value), self.assertRaises(ValueError):
                account(measured_case(), 1, value)

    def test_reject_double_accounting_and_invalid_results(self):
        for key, value in [('host_reduction_timed', True), ('correct', False),
                           ('M_padded', 768), ('kernel_including_upload_cycles', 0)]:
            row = measured_case(); row[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                account(row, 1, 0)


if __name__ == '__main__':
    unittest.main()
