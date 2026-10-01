"""Output-mode regressions, with the live game and encoder replaced by test doubles."""
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import render


class RenderOutputTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name) / "shot.mp4"
        self.api = Mock()
        self.api.game_render_mode.return_value = {"hwnd": 123, "rect": [0, 0, 64, 64]}
        state = {"state": {"sequence": {"fps": 60, "length": 20, "range": [4, 8],
                                       "audio": {"path": str(self.out)}}}}
        self.get = self.mock("_get", return_value=state)
        self.send = self.mock("send")
        self.wait_step = self.mock("wait_render_step", return_value=True)
        self.wait_stopped = self.mock("wait_render_clock_stopped", return_value=True)
        self.mock("time.sleep")
        self.capture = self.mock("control.capture_window", return_value=b"test png")
        self.which = self.mock("shutil.which", return_value=None)
        self.encoder = self.mock("subprocess.run", return_value=Mock(returncode=0))

    def mock(self, name, **kwargs):
        p = patch("render." + name, **kwargs)
        self.addCleanup(p.stop)
        return p.start()

    def run_render(self, **kwargs):
        return render.render(str(self.out), fps=30, width=64, height=64, api=self.api, **kwargs)

    def test_png_only_needs_no_ffmpeg_and_honors_range(self):
        self.assertTrue(self.run_render(output_format="png"))
        folder = Path(render.progress["out"])
        self.assertEqual(folder, self.out.with_name("shot_frames"))
        self.assertEqual(sorted(p.name for p in folder.iterdir()), ["f00000.png", "f00001.png", "f00002.png"])
        steps = [c for c in self.send.call_args_list if c.args[0] == "render_clock_step"]
        self.assertEqual([(c.kwargs["token"], c.kwargs["t"]) for c in steps],
                         [(1, 4), (2, 5), (3, 6), (4, 7), (5, 8)])
        self.assertEqual([c.kwargs["seconds"] for c in steps], [0, 1 / 60, 1 / 60, 1 / 60, 1 / 60])
        self.assertEqual([c.args[0] for c in self.send.call_args_list].count("render_clock_begin"), 1)
        self.assertEqual([c.args[0] for c in self.send.call_args_list].count("render_clock_end"), 1)
        self.wait_stopped.assert_called_once_with()
        self.which.assert_not_called()
        self.encoder.assert_not_called()
        self.assertFalse(self.out.exists())
        self.assertEqual(render.progress["stage"], "done")
        self.assertFalse(render.progress["running"])
        self.api.game_render_mode.assert_called_with(False, 64, 64)

    def test_rerender_uses_fresh_folder(self):
        self.run_render(output_format="png")
        old = Path(render.progress["out"])
        (old / "f00000.png").write_bytes(b"original")
        self.run_render(output_format="png")
        self.assertNotEqual(Path(render.progress["out"]), old)
        self.assertEqual((old / "f00000.png").read_bytes(), b"original")

    def test_capture_failure_preserves_completed_pngs(self):
        self.capture.side_effect = [b"first frame", RuntimeError("capture failed")]
        self.assertFalse(self.run_render(output_format="png"))
        self.assertEqual((Path(render.progress["out"]) / "f00000.png").read_bytes(), b"first frame")
        self.assertEqual(render.progress["frame"], 1)
        self.assertEqual(render.progress["stage"], "error")
        self.assertFalse(render.progress["running"])
        self.encoder.assert_not_called()
        self.api.game_render_mode.assert_called_with(False, 64, 64)

    def test_preparation_failure_clears_running(self):
        self.get.side_effect = RuntimeError("host disconnected")
        self.assertFalse(self.run_render(output_format="png"))
        self.assertFalse(render.progress["running"])
        self.assertIn("host disconnected", render.progress["error"])

    def test_mp4_still_encodes_and_can_keep_pngs(self):
        self.which.return_value = "ffmpeg"
        self.assertTrue(self.run_render(png_seq=True))
        self.assertEqual(self.encoder.call_count, 1)
        self.assertEqual(self.encoder.call_args.args[0][-1], str(self.out))
        self.assertEqual(render.progress["out"], str(self.out))
        self.assertEqual(len(list(Path(render.progress["png_dir"]).glob("f*.png"))), 3)

    def test_mp4_still_requires_ffmpeg(self):
        self.assertFalse(self.run_render())
        self.assertIn("ffmpeg", render.progress["error"])
        self.capture.assert_not_called()

    def test_24fps_uses_fractional_timeline_steps(self):
        self.assertTrue(render.render(str(self.out), fps=24, width=64, height=64, api=self.api, output_format="png"))
        steps = [c for c in self.send.call_args_list if c.args[0] == "render_clock_step"]
        self.assertEqual([c.kwargs["t"] for c in steps], [4, 5, 6, 6.5])
        self.assertEqual([c.kwargs["seconds"] for c in steps], [0, 1 / 60, 1 / 60, 0.5 / 60])

    def test_mp4_game_audio_runs_realtime_pass_and_muxes_it(self):
        self.which.return_value = "ffmpeg"

        def make_audio(path, *_args):
            path.write_bytes(b"wav")
            return 0.125, "Test speakers"

        audio_pass = self.mock("_capture_game_audio", side_effect=make_audio)
        self.assertTrue(self.run_render(game_audio=True))
        audio_pass.assert_called_once()
        self.assertEqual(render.progress["audio_device"], "Test speakers")
        self.assertEqual(self.encoder.call_count, 2)  # frame encode, then audio mux
        mux = self.encoder.call_args_list[-1].args[0]
        self.assertIn("game_mix.wav", " ".join(str(x) for x in mux))

    def test_external_guide_is_muxed_when_game_audio_is_off(self):
        self.which.return_value = "ffmpeg"
        self.out.write_bytes(b"guide placeholder")
        self.assertTrue(self.run_render())
        self.assertEqual(self.encoder.call_count, 2)
        mux = self.encoder.call_args_list[-1].args[0]
        self.assertIn("[guide]", mux)
        self.assertIn(str(self.out), mux)

    def test_png_sequence_skips_realtime_audio_pass(self):
        audio_pass = self.mock("_capture_game_audio")
        self.assertTrue(self.run_render(output_format="png", game_audio=True))
        audio_pass.assert_not_called()


if __name__ == "__main__":
    unittest.main()
