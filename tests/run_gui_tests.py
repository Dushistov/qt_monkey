#!/usr/bin/env python3

import subprocess, sys, io, json, re

def args_str_to_list(args_str):
    def append_to_args(args, arg):
        if arg.isdigit():
            args.append(int(arg))
        else:
            args.append(arg)

    whitespaces = re.compile(r"\s+")
    quote_param = re.compile(r'"(\\.|[^"])+"')
    single_param = re.compile(r"'(\\.|[^'])+'")
    res = []
    pos = 0
    while pos < len(args_str):
        m = re.match(whitespaces, args_str[pos:])
        if m:
            pos += m.end(0)
            continue
        if args_str[pos] == ',':
            pos += 1
            continue
        m = re.match(quote_param, args_str[pos:])
        if m:
            res.append(args_str[pos + m.start(0):pos + m.end(0)])
            pos += m.end(0)
            continue
        m = re.match(single_param, args_str[pos:])
        if m:
            res.append(args_str[pos + m.start(0):pos + m.end(0)])
            pos += m.end(0)
            continue
        new_pos = args_str.find(",", pos)
        if new_pos == -1:
            append_to_args(res, args_str[pos:])
            break
        else:
            append_to_args(res, args_str[pos:new_pos])
            pos = new_pos
    return res

def extract_func_name_and_params(line_with_func_call):
     func_m = re.match('(?P<func_prefix>[^\(]+)\((?P<args>.*)\);$', line_with_func_call)
     args_str = func_m.group("args")
     args = args_str_to_list(args_str)
     return (func_m.group("func_prefix"), args)

(prefix, params) = extract_func_name_and_params("Test.mouseClick('MainWindow.centralwidget.pushButton', 'Qt.LeftButton', 67, 13);")
print("params %s" % params)
assert prefix == "Test.mouseClick"
assert params == ["'MainWindow.centralwidget.pushButton'", "'Qt.LeftButton'", 67, 13]

def compare_two_func_calls(f1_call, f2_call):
    (pref1, params1) = extract_func_name_and_params(f1_call)
    (pref2, params2) = extract_func_name_and_params(f2_call)
    if pref1 != pref2 or len(params1) != len(params2):
        return False

    for p1, p2 in zip(params1, params2):
        if type(p1) is int and type(p2) is int:
            continue
        if p1 != p2:
            print("params not equal %s vs %s" % (p1, p2), file=sys.stderr)
            return False
    return True

qt_monkey_app_path = sys.argv[1]
test_app_path = sys.argv[2]
script_path = sys.argv[3]

monkey_cmd = [qt_monkey_app_path, "--script", script_path,
              "--exit-on-script-error",
              "--user-app", test_app_path]

monkey = subprocess.Popen(monkey_cmd, stdout=subprocess.PIPE,
                          stdin=subprocess.PIPE, stderr=sys.stderr)

code_listing = []
for line in io.TextIOWrapper(monkey.stdout, encoding="utf-8"):
    print("MONKEY: %s" % line)
#    print("Parsed json: %s" % json.loads(line))
    msg = json.loads(line)
    if type(msg) is dict:
        event = msg.get("event")
        if event:
            code = event["script"]
            code_listing.append(code)

with open(script_path, "r") as fin:
    i = 0
    for line in fin.readlines():
        if i >= len(code_listing):
            print("Unexpected end of actual result", file=sys.stderr)
            sys.exit(1)
        line = line.strip()
        if not compare_two_func_calls(line, code_listing[i]):
            print("Line %d, expected\n`%s'\n, actual\n`%s'\n" % (i + 1, line, code_listing[i]), file=sys.stderr)
            print("Full log:\n%s\n" % "\n".join(code_listing), file=sys.stderr)
            sys.exit(1)
        i += 1
