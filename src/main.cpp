#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "OcrProcessor.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    qmlRegisterType<OcrProcessor>("App", 1, 0, "OcrProcessor");

    QQmlApplicationEngine engine;
    // When building with qt6_add_qml_module the QML files are placed under
    // the module resource path. The generated cache registers the unit at
    // "/App/qml/Main.qml" (module URI = App). Load that path via qrc.
    const QUrl url(QStringLiteral("qrc:/App/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
