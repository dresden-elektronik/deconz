/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <unistd.h>
#include <signal.h>
#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QFile>
#include <QDir>

#include "deconz/u_platform.h"
#include "deconz/dbg_trace.h"
#include "deconz/util.h"
#include "deconz/zcl.h"
#include "zm_app.h"
#include "mainwindow.h"

#define APP_RET_RESTART_APP   41 // de_web_plugin_private.h
#define MAX_ARGS 32

static char cmdline[2048];

/*! Cleanup after signal.
    \param signo the signal number
 */
static void signal_cleanup_handler(int signo)
{
    DBG_Printf(DBG_INFO, "shutdown after signal(%d)\n", signo);
    // normal unix signals are always use return codes: 128 + SIGNUM
    // we forward this behavior on exit
    QCoreApplication::exit(128 + signo);
}

int main(int argc, char *argv[])
{
    if (signal(SIGINT, signal_cleanup_handler) == SIG_ERR)
    {
        DBG_Printf(DBG_ERROR, "failed to register SIGINT handler\n");
    }

    if (signal(SIGTERM, signal_cleanup_handler) == SIG_ERR)
    {
        DBG_Printf(DBG_ERROR, "failed to register SIGTERM handler\n");
    }

#ifdef PL_UNIX
    if (signal(SIGKILL, signal_cleanup_handler) == SIG_ERR)
    {
        DBG_Printf(DBG_ERROR, "failed to register SIGKILL handler\n");
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        DBG_Printf(DBG_ERROR, "failed to ignore SIGPIPE handler\n");
    }
#endif

    // detect if we run with -platform minimal (headless)
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-platform") == 0 &&
            argc > (i + 1) &&
            strcmp(argv[i + 1], "minimal") == 0)
        {
            gHeadlessVersion = true;
            break;
        }
    }

    DBG_Init(stdout);
    deCONZ::ApsMemory apsMem;
    deCONZ::ZclMemory zclMem;

    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; i++)
    {
        args.push_back(argv[i]);
    }

    bool pidInitialised = false;
    int exitCode = 0;

    // support soft reboot of application
    do {
        char *arg = cmdline;
        int argc2 = 0;
        char *argv2[MAX_ARGS] = { nullptr };

        DBG_Assert(args.size() < MAX_ARGS);

        for (size_t i = 0; i < args.size() && i < MAX_ARGS; i++)
        {
            DBG_Assert(strlen(cmdline) + args[i].size() < sizeof(cmdline));
            if (strlen(cmdline) + args[i].size() > sizeof (cmdline))
            {
                break; // not enough memory
            }

            argc2++;
            argv2[i] = arg;
            memcpy(arg, args[i].c_str(), args[i].size());
            arg[args[i].size()] = '\0';
            arg = arg + args[i].size() + 1;
        }

#if 0
// unfortunately this messes up node drawing and other widget stuff
        QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
        QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
        zmApp a(argc2, argv2);

        if (!pidInitialised) // needs to run after app instance created
        {
            pidInitialised = true;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            if (env.contains("XDG_RUNTIME_DIR"))
            {
                QDir dir;
                QString runPath(env.value("XDG_RUNTIME_DIR") + "/deconz");
                if (dir.mkpath(runPath))
                {
                    QFile f(runPath + "/deconz.pid");
                    if (f.open(QFile::WriteOnly))
                    {
                        QTextStream stream(&f);
                        stream << QCoreApplication::applicationPid();
                    }

                    QFile datadir(deCONZ::getStorageLocation(deCONZ::ApplicationsDataLocation));
                    if (datadir.exists())
                    {
                        datadir.link(runPath + "/data");
                    }
                }
            }
        }

        // enable debug output based on commandline arguments
        if (deCONZ::appArgumentNumeric("--dbg-error", 0) > 0)
        {
            DBG_Enable(DBG_ERROR);
        }

        if (deCONZ::appArgumentNumeric("--dbg-error", 0) > 1)
        {
            DBG_Enable(DBG_ERROR_L2);
        }

        if (deCONZ::appArgumentNumeric("--dbg-http", 0) > 0)
        {
            DBG_Enable(DBG_HTTP);
        }

        if (deCONZ::appArgumentNumeric("--dbg-info", 0) > 0)
        {
            DBG_Enable(DBG_INFO);
        }

        if (deCONZ::appArgumentNumeric("--dbg-info", 0) > 1)
        {
            DBG_Enable(DBG_INFO_L2);
        }

        if (deCONZ::appArgumentNumeric("--dbg-ota", 0) > 0)
        {
            DBG_Enable(DBG_OTA);
        }

        if (deCONZ::appArgumentNumeric("--dbg-aps", 0) > 0)
        {
            DBG_Enable(DBG_APS);

            if (deCONZ::appArgumentNumeric("--dbg-aps", 0) > 1)
            {
                DBG_Enable(DBG_APS_L2);
            }
        }

        if (deCONZ::appArgumentNumeric("--dbg-zdp", 0) > 0)
        {
            DBG_Enable(DBG_ZDP);
        }

        if (deCONZ::appArgumentNumeric("--dbg-ddf", 0) > 0)
        {
            DBG_Enable(DBG_DDF);
        }

        if (deCONZ::appArgumentNumeric("--dbg-dev", 0) > 0)
        {
            DBG_Enable(DBG_DEV);
        }

        if (deCONZ::appArgumentNumeric("--dbg-zcl", 0) > 0)
        {
            DBG_Enable(DBG_ZCL);
        }

        if (deCONZ::appArgumentNumeric("--dbg-zgp", 0) > 0)
        {
            DBG_Enable(DBG_ZGP);
        }

        if (deCONZ::appArgumentNumeric("--dbg-zcldb", 0) > 0)
        {
            DBG_Enable(DBG_ZCLDB);
        }

        if (deCONZ::appArgumentNumeric("--dbg-ias", 0) > 0)
        {
            DBG_Enable(DBG_IAS);
        }

        if (deCONZ::appArgumentNumeric("--dbg-route", 0) > 0)
        {
            DBG_Enable(DBG_ROUTING);
        }

        if (deCONZ::appArgumentNumeric("--dbg-prot", 0) > 0)
        {
            DBG_Enable(DBG_PROT);
        }

        if (deCONZ::appArgumentNumeric("--dbg-prot", 0) > 1)
        {
            DBG_Enable(DBG_PROT_L2);
        }

        if (deCONZ::appArgumentNumeric("--dbg-tlink", 0) > 0)
        {
            DBG_Enable(DBG_TLINK);
        }

        if (deCONZ::appArgumentNumeric("--dbg-wire", 0) > 0)
        {
            DBG_Enable(DBG_WIRE);
        }

        if (deCONZ::appArgumentNumeric("--dbg-js", 0) > 0)
        {
            DBG_Enable(DBG_JS);
        }

        if (deCONZ::appArgumentNumeric("--dbg-meas", 0) > 0)
        {
            DBG_Enable(DBG_MEASURE);
        }

        if (deCONZ::appArgumentNumeric("--dbg-vfs", 0) > 0)
        {
            DBG_Enable(DBG_VFS);
        }

        if (deCONZ::appArgumentNumeric("--dbg-fw", 0) > 0)
        {
            DBG_Enable(DBG_FIRMWARE);
        }

        QCoreApplication::setOrganizationName("dresden-elektronik");
        QCoreApplication::setOrganizationDomain("dresden-elektronik.de");
        QCoreApplication::setApplicationName("deCONZ");

        a.setApplicationVersion(QString("v%1.%2.%3%4").arg(APP_VERSION_MAJOR).arg(APP_VERSION_MINOR).arg(APP_VERSION_BUGFIX).arg(APP_CHANNEL));

        {
            QString dataLocation = deCONZ::getStorageLocation(deCONZ::ApplicationsLocation);

            if (!dataLocation.isEmpty() && !QFile::exists(dataLocation))
            {
                QDir dir(dataLocation);
                if (!dir.mkpath(dataLocation))
                {
                    DBG_Printf(DBG_ERROR, "failed to create %s\n", qPrintable(dataLocation));
                }
            }
        }

        {
            QString ddfUserLocation = deCONZ::getStorageLocation(deCONZ::DdfUserLocation);

            if (!ddfUserLocation.isEmpty() && !QFile::exists(ddfUserLocation))
            {
                QDir dir(ddfUserLocation);
                dir.mkpath(ddfUserLocation);
            }
        }

        {
            QString bundlesUserLocation = deCONZ::getStorageLocation(deCONZ::DdfBundleUserLocation);

            if (!bundlesUserLocation.isEmpty() && !QFile::exists(bundlesUserLocation))
            {
                QDir dir(bundlesUserLocation);
                dir.mkpath(bundlesUserLocation);
            }
        }

        MainWindow w;
        w.show();
        exitCode = a.exec();
    } while (exitCode == APP_RET_RESTART_APP);

    DBG_Destroy();

    return exitCode;
}
