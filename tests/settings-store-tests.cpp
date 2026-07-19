#include "core/settings-store.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

bool writeFile(const QString &path, const QByteArray &payload)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(payload) == payload.size();
}

QByteArray readFile(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.readAll();
}

dsk::PluginSettings sampleSettings(const QString &targetName)
{
	dsk::PluginSettings settings;
	dsk::OutputTarget target;
	target.id = QStringLiteral("settings-test-target");
	target.name = targetName;
	target.platformId = QStringLiteral("youtube");
	target.serverUrl = QStringLiteral("rtmps://example.test/live2");
	target.encoderGroup = dsk::EncoderGroup::DskVertical;
	target.sceneMode = dsk::TargetSceneMode::FixedScene;
	target.sceneName = QStringLiteral("Vertical Test");
	target.sceneUuid = QStringLiteral("vertical-test-uuid");
	target.sceneRoutes.push_back({QStringLiteral("Game"),
				       QStringLiteral("game-uuid"),
				       QStringLiteral("YouTube Game"),
				       QStringLiteral("youtube-game-uuid")});
	target.enabled = true;
	target.startWithAll = false;
	settings.targets.push_back(target);
	settings.followObsScene = true;
	settings.sceneLinks.push_back({QStringLiteral("Game"),
				       QStringLiteral("game-uuid"),
				       QStringLiteral("vertical-scene-id"),
				       {}});
	settings.verticalLayout.width = 1080;
	settings.verticalLayout.height = 1920;
	return settings;
}

void testRoundTripAndBackup()
{
	QTemporaryDir temp;
	check(temp.isValid(), "temporary directory is available");
	if (!temp.isValid())
		return;

	const QString path = QDir(temp.path()).filePath(QStringLiteral("dsk-multistream.json"));
	dsk::SettingsStore store(path);
	QString error;
	check(store.save(sampleSettings(QStringLiteral("First Name")), &error), "initial settings save succeeds");
	check(error.isEmpty(), "initial settings save has no error");

	dsk::SettingsStore reader(path);
	const dsk::PluginSettings loaded = reader.load();
	check(!reader.saveBlocked(), "valid settings do not block saves");
	check(reader.lastLoadWarning().isEmpty(), "valid settings do not emit a warning");
	check(loaded.targets.size() == 1, "valid settings preserve target count");
	check(loaded.targets.size() == 1 && loaded.targets[0].name == QStringLiteral("First Name"),
	      "valid settings preserve target name");
	check(loaded.targets.size() == 1 && loaded.targets[0].encoderGroup == dsk::EncoderGroup::DskVertical,
	      "valid settings preserve output mode");
	check(loaded.targets.size() == 1 && loaded.targets[0].sceneUuid == QStringLiteral("vertical-test-uuid"),
	      "valid settings preserve fixed scene UUID");
	check(loaded.targets.size() == 1 && loaded.targets[0].sceneRoutes.size() == 1 &&
		      loaded.targets[0].sceneRoutes[0].obsSceneUuid == QStringLiteral("game-uuid") &&
		      loaded.targets[0].sceneRoutes[0].outputSceneUuid == QStringLiteral("youtube-game-uuid"),
	      "valid settings preserve scene route UUIDs");
	check(loaded.sceneLinks.size() == 1 && loaded.sceneLinks[0].sceneUuid == QStringLiteral("game-uuid"),
	      "valid settings preserve vertical link UUID");

	error = QStringLiteral("stale error");
	check(store.save(sampleSettings(QStringLiteral("Second Name")), &error), "updated settings save succeeds");
	check(error.isEmpty(), "successful update clears a previous error string");
	const QString backupPath = path + QStringLiteral(".bak");
	check(QFile::exists(backupPath), "updated settings create a backup");
	const QJsonDocument backup = QJsonDocument::fromJson(readFile(backupPath));
	const QString backupName = backup.object().value(QStringLiteral("targets")).toArray().at(0).toObject().value(QStringLiteral("name")).toString();
	check(backupName == QStringLiteral("First Name"), "backup contains the previous settings revision");
}

void testCorruptSettingsRecovery()
{
	QTemporaryDir temp;
	check(temp.isValid(), "corrupt test temporary directory is available");
	if (!temp.isValid())
		return;

	const QString path = QDir(temp.path()).filePath(QStringLiteral("dsk-multistream.json"));
	const QByteArray corruptPayload("{ this is not json");
	check(writeFile(path, corruptPayload), "corrupt test file is created");

	dsk::SettingsStore store(path);
	const dsk::PluginSettings loaded = store.load();
	check(loaded.targets.isEmpty(), "corrupt settings load as empty defaults");
	check(!store.saveBlocked(), "successfully quarantined corrupt settings allow recovery save");
	check(!store.lastLoadWarning().isEmpty(), "corrupt settings produce a recovery warning");
	check(!QFile::exists(path), "corrupt settings are moved away from the active path");

	const QStringList quarantined = QDir(temp.path()).entryList(
		{QStringLiteral("dsk-multistream.json.corrupt-*.json")}, QDir::Files);
	check(quarantined.size() == 1, "exactly one quarantined settings file is created");
	if (quarantined.size() == 1)
		check(readFile(QDir(temp.path()).filePath(quarantined[0])) == corruptPayload,
		      "quarantined settings preserve the original bytes");

	QString error;
	check(store.save(sampleSettings(QStringLiteral("Recovered")), &error), "recovery settings save succeeds");
	check(QFile::exists(path), "recovery save recreates the active settings file");
}

void testNewerSchemaProtection()
{
	QTemporaryDir temp;
	check(temp.isValid(), "schema test temporary directory is available");
	if (!temp.isValid())
		return;

	const QString path = QDir(temp.path()).filePath(QStringLiteral("dsk-multistream.json"));
	QJsonObject root;
	root.insert(QStringLiteral("schemaVersion"), 99);
	root.insert(QStringLiteral("futureField"), QStringLiteral("must survive"));
	const QByteArray original = QJsonDocument(root).toJson(QJsonDocument::Compact);
	check(writeFile(path, original), "newer-schema test file is created");

	dsk::SettingsStore store(path);
	store.load();
	check(store.saveBlocked(), "newer settings schema blocks saves");
	check(store.lastLoadWarning().contains(QStringLiteral("newer schema"), Qt::CaseInsensitive),
	      "newer settings schema explains why saving is blocked");

	QString error;
	check(!store.save(sampleSettings(QStringLiteral("Must Not Save")), &error),
	      "newer settings schema cannot be overwritten");
	check(!error.isEmpty(), "blocked newer-schema save returns an error");
	check(readFile(path) == original, "blocked save preserves newer-schema bytes exactly");
}

void testStructurallyInvalidSettingsRecovery()
{
	QTemporaryDir temp;
	check(temp.isValid(), "structure test temporary directory is available");
	if (!temp.isValid())
		return;

	const QString path = QDir(temp.path()).filePath(QStringLiteral("dsk-multistream.json"));
	QJsonObject root;
	root.insert(QStringLiteral("schemaVersion"), 1);
	root.insert(QStringLiteral("targets"), QJsonObject{{QStringLiteral("not"), QStringLiteral("an array")}});
	const QByteArray original = QJsonDocument(root).toJson(QJsonDocument::Compact);
	check(writeFile(path, original), "structurally invalid test file is created");

	dsk::SettingsStore store(path);
	const dsk::PluginSettings loaded = store.load();
	check(loaded.targets.isEmpty(), "structurally invalid settings load as empty defaults");
	check(!store.saveBlocked(), "quarantined structural corruption allows recovery save");
	check(store.lastLoadWarning().contains(QStringLiteral("field types"), Qt::CaseInsensitive),
	      "structural corruption warning identifies the invalid schema");
	const QStringList quarantined = QDir(temp.path()).entryList(
		{QStringLiteral("dsk-multistream.json.corrupt-*.json")}, QDir::Files);
	check(quarantined.size() == 1, "structural corruption creates one quarantine file");
	if (quarantined.size() == 1)
		check(readFile(QDir(temp.path()).filePath(quarantined[0])) == original,
		      "structural corruption quarantine preserves original bytes");
}

void testFractionalSchemaRecovery()
{
	QTemporaryDir temp;
	check(temp.isValid(), "fractional schema test temporary directory is available");
	if (!temp.isValid())
		return;

	const QString path = QDir(temp.path()).filePath(QStringLiteral("dsk-multistream.json"));
	QJsonObject root;
	root.insert(QStringLiteral("schemaVersion"), 1.5);
	root.insert(QStringLiteral("targets"), QJsonArray{});
	const QByteArray original = QJsonDocument(root).toJson(QJsonDocument::Compact);
	check(writeFile(path, original), "fractional schema test file is created");

	dsk::SettingsStore store(path);
	const dsk::PluginSettings loaded = store.load();
	check(loaded.targets.isEmpty(), "fractional schema settings load as empty defaults");
	check(!store.saveBlocked(), "fractional schema is quarantined instead of blocking recovery");
	check(store.lastLoadWarning().contains(QStringLiteral("positive integer"), Qt::CaseInsensitive),
	      "fractional schema warning identifies the version problem");
	const QStringList quarantined = QDir(temp.path()).entryList(
		{QStringLiteral("dsk-multistream.json.corrupt-*.json")}, QDir::Files);
	check(quarantined.size() == 1, "fractional schema creates one quarantine file");
}

void testBackupFailureBlocksOverwrite()
{
	QTemporaryDir temp;
	check(temp.isValid(), "backup failure test temporary directory is available");
	if (!temp.isValid())
		return;

	const QString path = QDir(temp.path()).filePath(QStringLiteral("dsk-multistream.json"));
	dsk::SettingsStore store(path);
	QString error;
	check(store.save(sampleSettings(QStringLiteral("Preserve Me")), &error),
	      "backup failure test initial save succeeds");
	const QByteArray original = readFile(path);
	check(QDir().mkdir(path + QStringLiteral(".bak")),
	      "backup failure test blocks the backup path with a directory");

	error.clear();
	check(!store.save(sampleSettings(QStringLiteral("Must Not Replace")), &error),
	      "backup failure prevents the active settings overwrite");
	check(!error.isEmpty(), "backup failure returns a user-facing error");
	check(readFile(path) == original, "backup failure preserves the active settings bytes");
}

void testNestedTypeCorruptionRecovery()
{
	QTemporaryDir temp;
	check(temp.isValid(), "nested type test temporary directory is available");
	if (!temp.isValid())
		return;

	const QString path = QDir(temp.path()).filePath(QStringLiteral("dsk-multistream.json"));
	QJsonObject target;
	target.insert(QStringLiteral("id"), QStringLiteral("bad-target"));
	target.insert(QStringLiteral("name"), 42);
	QJsonObject root;
	root.insert(QStringLiteral("schemaVersion"), 1);
	root.insert(QStringLiteral("targets"), QJsonArray{target});
	const QByteArray original = QJsonDocument(root).toJson(QJsonDocument::Compact);
	check(writeFile(path, original), "nested type corruption test file is created");

	dsk::SettingsStore store(path);
	store.load();
	check(!store.saveBlocked(), "nested type corruption is quarantined for recovery");
	check(store.lastLoadWarning().contains(QStringLiteral("target fields"), Qt::CaseInsensitive),
	      "nested target type warning identifies the invalid object");
	const QStringList quarantined = QDir(temp.path()).entryList(
		{QStringLiteral("dsk-multistream.json.corrupt-*.json")}, QDir::Files);
	check(quarantined.size() == 1, "nested type corruption creates one quarantine file");
	if (quarantined.size() == 1)
		check(readFile(QDir(temp.path()).filePath(quarantined[0])) == original,
		      "nested type quarantine preserves original bytes");
}

} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	testRoundTripAndBackup();
	testCorruptSettingsRecovery();
	testNewerSchemaProtection();
	testStructurallyInvalidSettingsRecovery();
	testFractionalSchemaRecovery();
	testBackupFailureBlocksOverwrite();
	testNestedTypeCorruptionRecovery();

	if (failures > 0) {
		std::cerr << failures << " settings-store checks failed.\n";
		return 1;
	}

	std::cout << "All settings-store tests passed.\n";
	return 0;
}
