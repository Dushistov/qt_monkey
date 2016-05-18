Test.log("start 1");
try {
    Test.mouseClick('MainWindow.menubar', 'Qt.LeftButton', 25, 12);
} catch (err) {
    Test.log("no menubar, Mac OS X?");
}
Test.activateItem('MainWindow.menubar.menuFiles', 'Quit');
<<<RESTART FROM HERE>>>
Test.log("start 2");
try {
    Test.mouseClick('MainWindow.menubar', 'Qt.LeftButton', 25, 12);
} catch (err) {
    Test.log("no menubar 2, Mac OS X?");
}
Test.activateItem('MainWindow.menubar.menuFiles', 'Quit');
