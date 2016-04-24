#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <QtCore/QCoreApplication>
#include <QtCore/QTextStream>

#include "common.hpp"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTextStream cout(stdout);
    QTextStream cerr(stderr);

    int user_app_offset = -1;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--user-app") == 0) {
            if ((argc + 1) >= argc) {
                cerr << T_("Usage: %s --run-script path/to/script path/to/application [application's command line args]\n").arg(argv[0]);
                return EXIT_FAILURE;
            }
            user_app_offset = argc + 1;
            break;
        } else {
            cerr << T_("Unkown option %s\n").arg(argv[i]);
            return EXIT_FAILURE;
        }

    return app.exec();
}
