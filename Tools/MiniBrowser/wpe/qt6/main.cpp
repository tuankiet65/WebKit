/*
 * Copyright (C) 2018, 2019 Igalia S.L
 * Copyright (C) 2018, 2019 Zodiac Inflight Innovations
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

class UrlHelper : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE QUrl parseUserUrl(const QString &input) {
        return QUrl::fromUserInput(input);
    }
};

int main(int argc, char* argv[])
{
#if defined(WEBKIT_INJECTED_BUNDLE_PATH)
    setenv("WEBKIT_INJECTED_BUNDLE_PATH", WEBKIT_INJECTED_BUNDLE_PATH, 0);
#endif
#if defined(WEBKIT_INSPECTOR_RESOURCES_PATH)
    setenv("WEBKIT_INSPECTOR_RESOURCES_PATH", WEBKIT_INSPECTOR_RESOURCES_PATH, 0);
#endif

    QGuiApplication app(argc, argv);

    QCommandLineParser parser;
    QCoreApplication::setApplicationVersion("0.1");
    parser.setApplicationDescription(QGuiApplication::applicationDisplayName());
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("initialUrl", "The URL to open.");
    QStringList arguments = app.arguments();
    parser.process(arguments);
    const QString userInput = parser.positionalArguments().isEmpty() ?
        QStringLiteral("https://wpewebkit.org") : parser.positionalArguments().first();

    QQmlApplicationEngine engine;
    QQmlContext* context = engine.rootContext();
    QUrl initialUrl = QUrl::fromUserInput(userInput);
    context->setContextProperty(QStringLiteral("initialUrl"), initialUrl);

    UrlHelper urlHelper;
    context->setContextProperty(QStringLiteral("urlHelper"), &urlHelper);

    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}

#include "main.moc"
