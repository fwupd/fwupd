/*
 * Copyright (C) 2020 Aleix Pol <aleixpol@kde.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <fwupd.h>

#include <QCoreApplication>
#include <QtConcurrentRun>
#include <QFutureWatcher>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    auto client = fwupd_client_new();
    auto cancellable = g_cancellable_new();
    g_autoptr(GError) error = nullptr;

    auto fw = new QFutureWatcher<GPtrArray*>(&app);
    QObject::connect(fw, &QFutureWatcher<GPtrArray*>::finished, [fw]() {
        QCoreApplication::exit(0);
    });
    fw->setFuture(QtConcurrent::run([&] {
        return fwupd_client_get_devices(client, cancellable, &error);
    }));

    return app.exec();
}
