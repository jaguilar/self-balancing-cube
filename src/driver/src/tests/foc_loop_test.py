import testrunner

testrunner.run_test("FocLoopOpenLoop", timeout_s=20.0)
testrunner.run_test("FocLoopTorqueControl", timeout_s=30.0)
