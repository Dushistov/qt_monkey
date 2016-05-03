Qt Monkey
=========

About
-----
`Qt Monkey` is tool to automate testing of Qt based (widgets mainly) applications.
It automates creation/modification and running of tests.
Tests are written on `Javascript` (`Qt`'s variant).
To write tests just run your application (instrumented by `Qt Monkey`) and click
on widget, input text etc. As result, script on `Javascript` was generated
(see example https://github.com/Dushistov/qt_monkey/blob/master/tests/test1.js).
After that you can add suitable asserts and run this script via command line or GUI tool.

How to use
----------
You need build and link qtmonkey_agent library with your application.
After that create `qt_monkey_agent::Agent` like this
::
   qt_monkey_agent::Agent agent;

That's all. After that your can run `qtmonkey_gui` application,
and record or run your scripts. See https://github.com/Dushistov/qt_monkey/blob/master/tests/main.cpp
for more complex usage example.

Internals
---------

`Qt Monkey` consists of three parts: `Qt Monkey agent library`, `qtmonkey_app`,
tool that cooperate with `agent` and `qtmonkey_gui`.
`qtmonkey_app` cooperate with `qtmonkey_gui` via stdin/stdout streams.
Data in this streams structured via `json`. So you can replace `qtmonkey_gui`
with plugin of your `IDE`.

Requirements
------------

`Qt Monkey` require Qt 4.x or Qt 5.x and C++ compiler with C++11 support.
