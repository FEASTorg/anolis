#!/usr/bin/env python3
"""
TSAN/CTest Log Summarizer
Parses CTest output logs and generates a clean summary with filtered noise.
Usage:
  python scripts/summarize_tsan_log.py <log_file> [output_file]
Example:
    python scripts/summarize_tsan_log.py tsan-test.log
    python scripts/summarize_tsan_log.py tsan-test.log tsan-test-cleaned.log
"""

import re
from pathlib import Path


class TestLogParser:
    def __init__(self, log_file):
        self.log_file = log_file
        self.tests = []
        self.failed_tests = []
        self.passed_tests = []
        self.segfault_tests = []
        self.test_count = 0
        self.pass_rate = 0.0

    def parse(self):
        """Parse the log file and extract test results."""
        with open(self.log_file, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()

        # Extract summary information
        summary_match = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", content)
        if summary_match:
            self.pass_rate = float(summary_match.group(1))
            int(summary_match.group(2))
            self.test_count = int(summary_match.group(3))

        # Extract individual test results
        test_pattern = r"^\s*(\d+)/\d+ Test\s+#(\d+):\s+(.+?)\s+\.+\s+(Passed|Failed|\*\*\*Failed)\s+([\d.]+) sec"
        for match in re.finditer(test_pattern, content, re.MULTILINE):
            test_num = int(match.group(2))
            test_name = match.group(3).strip()
            status = match.group(4)
            duration = float(match.group(5))

            test_info = {
                "number": test_num,
                "name": test_name,
                "status": status,
                "duration": duration,
            }

            self.tests.append(test_info)

            if "Passed" in status:
                self.passed_tests.append(test_info)
            else:
                self.failed_tests.append(test_info)

        # Extract failed test details with failure reasons
        failed_section = re.search(
            r"The following tests FAILED:(.*?)(?:Errors while running CTest|$)",
            content,
            re.DOTALL,
        )
        if failed_section:
            failure_pattern = r"\s*(\d+)\s+-\s+(.+?)\s+\((\w+)\)"
            for match in re.finditer(failure_pattern, failed_section.group(1)):
                test_num = int(match.group(1))
                test_name = match.group(2).strip()
                failure_type = match.group(3)

                if failure_type == "SEGFAULT":
                    self.segfault_tests.append(
                        {
                            "number": test_num,
                            "name": test_name,
                            "failure_type": failure_type,
                        }
                    )

    def extract_test_output(self, test_number):
        """Extract the output for a specific test number."""
        with open(self.log_file, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()

        # Pattern to match test section
        pattern = rf"^test {test_number}\n.*?^{test_number}/\d+ Test"
        match = re.search(pattern, content, re.MULTILINE | re.DOTALL)

        if match:
            lines = match.group(0).split("\n")
            # Filter out noisy lines
            filtered = []
            for line in lines:
                # Keep test result lines and error lines
                if any(
                    marker in line
                    for marker in [
                        "[ RUN      ]",
                        "[  FAILED  ]",
                        "[       OK ]",
                        "[  PASSED  ]",
                        "SEGFAULT",
                        "ERROR",
                        "WARNING: ThreadSanitizer",
                    ]
                ):
                    filtered.append(line)
            return "\n".join(filtered)
        return None

    def generate_summary(self):
        """Generate a summary report."""
        lines = []
        lines.append("=" * 70)
        lines.append("TEST EXECUTION SUMMARY")
        lines.append("=" * 70)
        lines.append(f"Total Tests:        {self.test_count}")
        lines.append(f"Passed:             {len(self.passed_tests)} ({self.pass_rate:.1f}%)")
        lines.append(f"Failed:             {len(self.failed_tests)}")
        lines.append(f"  - Segfaults:      {len(self.segfault_tests)}")
        lines.append("=" * 70)

        if self.segfault_tests:
            lines.append("\nFAILED TESTS (SEGFAULT):")
            lines.append("-" * 70)
            for test in self.segfault_tests:
                lines.append(f"  Test #{test['number']:3d}: {test['name']}")
            lines.append("")

        # Show slowest tests
        slowest = sorted(self.tests, key=lambda x: x["duration"], reverse=True)[:10]
        lines.append("\nTOP 10 SLOWEST TESTS:")
        lines.append("-" * 70)
        for test in slowest:
            lines.append(f"  {test['duration']:6.2f}s - Test #{test['number']:3d}: {test['name']}")

        lines.append("\n" + "=" * 70)

        return "\n".join(lines)

    def generate_clean_log(self, output_file):
        """Generate a cleaned log file with reduced noise."""
        with open(output_file, "w") as f:
            # Write summary
            f.write(self.generate_summary())
            f.write("\n\n")

            # Write only failed test details
            if self.failed_tests:
                f.write("=" * 70 + "\n")
                f.write("FAILED TEST DETAILS\n")
                f.write("=" * 70 + "\n\n")

                for test in self.failed_tests:
                    f.write(f"\n{'-' * 70}\n")
                    f.write(f"Test #{test['number']}: {test['name']}\n")
                    f.write(f"{'-' * 70}\n")

                    output = self.extract_test_output(test["number"])
                    if output:
                        f.write(output + "\n")
                    else:
                        f.write("(No detailed output available)\n")


def main():
    import sys

    if len(sys.argv) < 2:
        print("Usage: python parse_tsan_log.py <log_file> [output_file]")
        print("\nExample:")
        print("  python parse_tsan_log.py tsan-test.log")
        print("  python parse_tsan_log.py tsan-test.log cleaned-output.log")
        sys.exit(1)

    log_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else "cleaned_test_log.txt"

    if not Path(log_file).exists():
        print(f"Error: Log file '{log_file}' not found")
        sys.exit(1)

    print(f"Parsing log file: {log_file}")
    parser = TestLogParser(log_file)
    parser.parse()

    # Print summary to console
    print("\n" + parser.generate_summary())

    # Generate cleaned log file
    print(f"\nGenerating cleaned log: {output_file}")
    parser.generate_clean_log(output_file)
    print("Done!")


if __name__ == "__main__":
    main()
