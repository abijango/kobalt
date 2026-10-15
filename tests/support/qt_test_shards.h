#pragma once
#include <QMetaMethod>
#include <QObject>
#include <QStringList>

namespace kobalt::test {
inline bool shardContains(int position, const char *variable) {
    const auto shard = qEnvironmentVariable(variable);
    if (shard.isEmpty()) {
        return true;
    }
    const auto parts = shard.split('/');
    bool indexOk = false, countOk = false;
    const int index = parts.value(0).toInt(&indexOk), count = parts.value(1).toInt(&countOk);
    if (parts.size() != 2 || !indexOk || !countOk || index < 0 || count < 1 || index >= count) {
        qFatal("%s must be INDEX/COUNT, with 0 <= INDEX < COUNT", variable);
    }
    return position % count == index;
}

// QtTest runs each selected slot with its data rows and normal setup/cleanup.
// Discover slots from the compiled object so new tests enter a shard automatically.
inline QStringList shardArguments(const QObject &test, int argc, char **argv) {
    QStringList arguments;
    for (int i = 0; i < argc; ++i) {
        arguments.append(QString::fromLocal8Bit(argv[i]));
    }
    const auto shard = qEnvironmentVariable("KOBALT_TEST_SHARD");
    if (shard.isEmpty()) {
        return arguments;
    }
    const auto excluded = qEnvironmentVariable("KOBALT_TEST_EXCLUDE").split(',');
    const auto *meta = test.metaObject();
    int selected = 0, position = 0;
    for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
        const auto method = meta->method(i);
        const auto name = method.name();
        if (method.methodType() != QMetaMethod::Slot || method.parameterCount() != 0 ||
            method.returnType() != QMetaType::Void || name.endsWith("_data") || name == "initTestCase" ||
            name == "cleanupTestCase" || name == "init" || name == "cleanup" ||
            excluded.contains(QString::fromLatin1(name))) {
            continue;
        }
        if (shardContains(position++, "KOBALT_TEST_SHARD")) {
            arguments.append(QString::fromLatin1(name));
            ++selected;
        }
    }
    if (!selected) {
        qFatal("Empty QtTest shard");
    }
    return arguments;
}
} // namespace kobalt::test
