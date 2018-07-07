#!/usr/bin/env python

import subprocess, sys, codecs

qt_monkey_app_path = sys.argv[1]
test_app_path = sys.argv[2]
script_path = sys.argv[3]

monkey_cmd = [qt_monkey_app_path, "--script", script_path,
              "--exit-on-script-error",
              "--user-app", test_app_path]

monkey = subprocess.Popen(monkey_cmd, stdout=subprocess.PIPE,
                          stdin=subprocess.PIPE, stderr=sys.stderr)
input_stream = codecs.getreader("utf-8")(monkey.stdout)
lines = [line.strip() for line in input_stream]
if '{"script logs": "start 1"}' in lines \
   and '{"script logs": "start 2"}' in lines:
    sys.exit(0)
else:
    sys.stderr.write("output of application not contains suitable lines\n")
    sys.stderr.write("\n".join(lines) + "\n")
    sys.exit(1)
