#ifdef QT_QML_DEBUG
#include <QtQuick>
#endif

#include <sailfishapp.h>

#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QScopedPointer>

#include "accountsmanager.h"
#include "accountsmodel.h"
#include "checklistparser.h"
#include "foldersmodel.h"
#include "networkmonitor.h"
#include "notesdatabase.h"
#include "notesmanager.h"
#include "notesmodel.h"
#include "richtextconverter.h"
#include "settings.h"
#include "syncengine.h"

int main(int argc, char *argv[])
{
    // OrganizationName == ApplicationName == target name. QStandardPaths then
    // resolves AppDataLocation to
    // ~/.local/share/harbour-imap-notes/harbour-imap-notes/ which matches the
    // path users expect for a Harbour-style app.
    QCoreApplication::setOrganizationName(QStringLiteral("harbour-imap-notes"));
    QCoreApplication::setApplicationName(QStringLiteral("harbour-imap-notes"));

    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    QScopedPointer<QQuickView> view(SailfishApp::createView());

    NotesDatabase *database = new NotesDatabase(app.data());
    if (!database->open()) {
        qFatal("Cannot open notes database at %s", qPrintable(database->path()));
    }

    Settings *settings = new Settings(app.data());
    NetworkMonitor *network = new NetworkMonitor(app.data());
    AccountsManager *accounts = new AccountsManager(database, app.data());
    NotesManager *notes = new NotesManager(database, app.data());
    SyncEngine *sync = new SyncEngine(database, accounts, network, settings, app.data());

    AccountsModel *accountsModel = new AccountsModel(database, accounts, app.data());
    FoldersModel *foldersModel = new FoldersModel(database, app.data());
    NotesModel *notesModel = new NotesModel(database, app.data());

    // Sync writes happen on a worker thread / connection; the UI-side models
    // need an explicit reload signal because they don't see those writes
    // through their own connection's change notifier.
    QObject::connect(sync, &SyncEngine::databaseChanged, [accountsModel, foldersModel, notesModel]() {
        accountsModel->reload();
        foldersModel->reload();
        notesModel->reload();
    });
    QObject::connect(notes, &NotesManager::notesChanged, notesModel, &NotesModel::reload);
    QObject::connect(notes, &NotesManager::foldersChanged, foldersModel, &FoldersModel::reload);

    QQmlContext *ctx = view->rootContext();
    ctx->setContextProperty(QStringLiteral("AppSettings"), settings);
    ctx->setContextProperty(QStringLiteral("Accounts"), accounts);
    ctx->setContextProperty(QStringLiteral("Notes"), notes);
    ctx->setContextProperty(QStringLiteral("Network"), network);
    ctx->setContextProperty(QStringLiteral("Sync"), sync);
    ctx->setContextProperty(QStringLiteral("accountsModel"), accountsModel);
    ctx->setContextProperty(QStringLiteral("foldersModel"), foldersModel);
    ctx->setContextProperty(QStringLiteral("notesModel"), notesModel);

    qmlRegisterSingletonType<RichTextConverter>("harbour.imapnotes", 1, 0, "RichText",
        [](QQmlEngine *, QJSEngine *) -> QObject * { return new RichTextConverter; });
    qmlRegisterSingletonType<ChecklistParser>("harbour.imapnotes", 1, 0, "Checklist",
        [](QQmlEngine *, QJSEngine *) -> QObject * { return new ChecklistParser; });

    view->setSource(SailfishApp::pathToMainQml());
    view->show();
    return app->exec();
}
