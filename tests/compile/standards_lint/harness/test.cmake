# Included by mc_add_test in cmake/targets.cmake. The harness's self test runs on the harness, so if the harness stopped
# counting failures, the self test's own failed checks would go uncounted too, and it would pass. The verdict on the
# samples therefore comes from outside: their run must exit non-zero, and ctest is the one that says so.
add_test(NAME compile_standards_lint_harness_samples_fail
  COMMAND mc_test_compile_standards_lint_harness --samples)
set_tests_properties(compile_standards_lint_harness_samples_fail PROPERTIES WILL_FAIL TRUE TIMEOUT 60)
