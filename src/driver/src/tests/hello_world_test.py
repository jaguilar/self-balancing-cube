import testrunner

transcript = testrunner.run_test("HelloWorld")
assert "Hello, World!" in transcript, f"Expected 'Hello, World!' in transcript:\n{transcript}"
