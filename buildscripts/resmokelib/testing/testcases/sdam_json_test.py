"""The unittest.TestCase for Server Discovery and Monitoring JSON tests."""
import os
from . import interface
from ... import core
from ... import utils
from ...utils import globstar
from ... import errors


class SDAMJsonTestCase(interface.ProcessTestCase):
    """Server Discovery and Monitoring JSON test case."""

    REGISTERED_NAME = "sdam_json_test"
    EXECUTABLE_BUILD_PATH = "build/**/mongo/client/sdam/sdam_json_test"
    TEST_DIR = "src/mongo/client/sdam/json_tests"

    def __init__(self, logger, json_test_file, program_options=None):
        """Initialize the TestCase with the executable to run."""
        interface.ProcessTestCase.__init__(self, logger, "SDAM Json Test", json_test_file)

        self.program_executable = self._find_executable()
        self.json_test_file = json_test_file
        self.program_options = utils.default_if_none(program_options, {}).copy()

    def _find_executable(self):
        execs = globstar.glob(self.EXECUTABLE_BUILD_PATH + '.exe')
        if not execs:
            execs = globstar.glob(self.EXECUTABLE_BUILD_PATH)
        if len(execs) != 1:
            raise errors.StopExecution(
                "There must be a single sdam_json_test binary in {}".format(execs))
        return execs[0]

    def _make_process(self):
        command_line = [self.program_executable]
        command_line += ["--source-dir", self.TEST_DIR]
        command_line += ["-f", os.path.basename(self.json_test_file)]
        return core.programs.make_process(self.logger, command_line)
