import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / 'tools/chromium/playback-check.py'
SPEC = importlib.util.spec_from_file_location('playback_check', SCRIPT)
playback_check = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(playback_check)


class ContinuityTests(unittest.TestCase):
    def test_rejects_alternating_original_frames_after_warmup(self):
        with self.assertRaisesRegex(RuntimeError, 'not continuous'):
            playback_check.validate_vsr_continuity([
                {'total':120,'original_window':50},
                {'total':240,'original_window':60}])

    def test_accepts_initial_warmup_followed_by_complete_windows(self):
        playback_check.validate_vsr_continuity([
            {'total':120,'original_window':30},
            {'total':240,'original_window':0},
            {'total':360,'original_window':0}])

    def test_requires_a_post_warmup_window(self):
        with self.assertRaisesRegex(RuntimeError, 'no post-warmup'):
            playback_check.validate_vsr_continuity([{'total':120,'original_window':0}])


class BrowserHealthTests(unittest.TestCase):
    def test_rejects_gpu_crash_even_when_playback_recovers(self):
        with self.assertRaisesRegex(RuntimeError, 'GPU process crashed'):
            playback_check.validate_browser_health(
                'GPU process exited unexpectedly: exit_code=139\n'
                'Reinitialized the GPU process after a crash')

    def test_accepts_log_without_gpu_crash(self):
        playback_check.validate_browser_health('Playback initialized')


class EnhancedSelectionTests(unittest.TestCase):
    def test_one_warmup_selection_does_not_prove_sustained_processing(self):
        with self.assertRaisesRegex(RuntimeError, 'insufficient enhanced frames'):
            playback_check.validate_vsr_selection([{'selected_enhanced': 1}])

    def test_accepts_sustained_renderer_selection_milestone(self):
        playback_check.validate_vsr_selection([
            {'selected_enhanced': 1}, {'selected_enhanced': 120}])

    def test_watchdog_shutdown_fails_even_after_selection_milestone(self):
        with self.assertRaisesRegex(RuntimeError, 'watchdog disabled'):
            playback_check.validate_vsr_selection(
                [{'selected_enhanced': 120}],
                browser_log='NVIDIA VSR watchdog disabled session: running job exceeded 100ms')

    def test_extracts_sparse_renderer_selection_messages(self):
        events = [{
            'method': 'Media.playerMessagesLogged',
            'params': {'messages': [{
                'level': 'info',
                'message': 'NVIDIA VSR selected_enhanced=120 generation=4 frame_id=927',
            }]},
        }]

        self.assertEqual([{
            'selected_enhanced': 120,
            'generation': 4,
            'frame_id': 927,
        }], playback_check.extract_enhanced_selections(events))

    def test_completion_or_media_event_alone_does_not_prove_selection(self):
        events = [
            {'method': 'Media.playerEventsAdded', 'params': {'events': [{
                'value': 'NVIDIA VSR completed=12',
            }]}},
            {'method': 'Media.playerMessagesLogged', 'params': {'messages': [{
                'level': 'info', 'message': 'NVIDIA VSR completed=12',
            }]}},
            {'method': 'Media.playerMessagesLogged', 'params': {'messages': [{
                'level': 'info',
                'message': 'NVIDIA VSR selected_enhanced=0 generation=4 frame_id=927',
            }]}},
        ]

        selections = playback_check.extract_enhanced_selections(events)
        self.assertEqual([], selections)
        with self.assertRaisesRegex(RuntimeError, 'no enhanced frame was selected'):
            playback_check.validate_vsr_selection(selections)


if __name__ == '__main__':
    unittest.main()
