// This file is part of Gram Wallet Desktop,
// a desktop application for the TON Blockchain project.
//
// For license and copyright information please follow this link:
// https://github.com/ton-blockchain/wallet-desktop/blob/master/LEGAL
//
#include "core/launcher.h"

#include "ui/main_queue_processor.h"
#include "ui/ui_utility.h"
#include "core/sandbox.h"
#include "core/version.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/base_platform_url_scheme.h"
#include "base/concurrent_timer.h"

#ifdef WALLET_AUTOUPDATING_BUILD
#include "updater/updater_instance.h"
#endif // WALLET_AUTOUPDATING_BUILD

#include <QtWidgets/QApplication>
#include <QtCore/QJsonObject>
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <iostream>

namespace Core {
namespace {

// 8 hour min time between update check requests.
constexpr auto kUpdaterDelayConstPart = 8 * 3600;

// 8 hour max - min time between update check requests.
constexpr auto kUpdaterDelayRandPart = 8 * 3600;

class FilteredCommandLineArguments {
public:
	FilteredCommandLineArguments(int argc, char **argv);

	int &count();
	char **values();

private:
	static constexpr auto kForwardArgumentCount = 1;

	int _count = 0;
	char *_arguments[kForwardArgumentCount + 1] = { nullptr };

};

FilteredCommandLineArguments::FilteredCommandLineArguments(
	int argc,
	char **argv)
: _count(std::clamp(argc, 0, kForwardArgumentCount)) {
	// For now just pass only the first argument, the executable path.
	for (auto i = 0; i != _count; ++i) {
		_arguments[i] = argv[i];
	}
}

int &FilteredCommandLineArguments::count() {
	return _count;
}

char **FilteredCommandLineArguments::values() {
	return _arguments;
}

#ifdef WALLET_AUTOUPDATING_BUILD
Updater::InfoForRegistry GetInfoForRegistry() {
	auto result = Updater::InfoForRegistry();
	result.fullName = "Gram Wallet";
	result.guid = "5ED3C1CA-9AA7-4884-B01A-21D3A0CD0FB4";
	result.helpLink
		= result.supportLink
		= result.updateLink
		= "https://desktop.ton.org";
	result.iconGroup = "Gram Wallet";
	result.publisher = "Telegram FZ-LLC";
	return result;
}
#endif // WALLET_AUTOUPDATING_BUILD

base::Platform::UrlSchemeDescriptor CustomSchemeDescriptor(
		not_null<Launcher*> launcher) {
	auto result = base::Platform::UrlSchemeDescriptor();
	result.executable = base::Integration::Instance().executablePath();
	result.protocol = "ton";
	result.protocolName = "TON Gram Transfer Link";
	result.shortAppName = "gramwallet";
	result.desktopFileDir = launcher->workingPath();
	result.desktopFileName = "gramwallet";
	result.iconFileName = "gramwallet";
	result.longAppName = "GramWallet";
	result.displayAppName = "Gram Wallet";
	result.displayAppDescription = "Desktop wallet for TON";
	return result;
}

} // namespace

std::unique_ptr<Launcher> Launcher::Create(int argc, char *argv[]) {
	return std::make_unique<Launcher>(argc, argv);
}

Launcher::Launcher(int argc, char *argv[])
: _argc(argc)
, _argv(argv)
, _baseIntegration(argc, argv) {
	base::Integration::Set(&_baseIntegration);
}

Launcher::~Launcher() = default;

void Launcher::init() {
	QApplication::setApplicationName("Gram Wallet");
	initAppDataPath();
	initWorkingPath();
}

void Launcher::initWorkingPath() {
	_workingPath = computeWorkingPathBase() + "data/";
}

QString Launcher::computeWorkingPathBase() {
	if (const auto path = checkPortablePath(); !path.isEmpty()) {
		return path;
	}
#if defined Q_OS_MAC || defined Q_OS_LINUX
#if defined _DEBUG && !defined OS_MAC_STORE
	return _baseIntegration.executableDir();
#else // _DEBUG
	return _appDataPath;
#endif // !_DEBUG
#elif defined OS_WIN_STORE // Q_OS_MAC || Q_OS_LINUX
#ifdef _DEBUG
	return _baseIntegration.executableDir();
#else // _DEBUG
	return _appDataPath;
#endif // !_DEBUG
#elif defined Q_OS_WIN
	if (canWorkInExecutablePath()) {
		return _baseIntegration.executableDir();
	} else {
		return _appDataPath;
	}
#endif // Q_OS_MAC || Q_OS_LINUX || Q_OS_WINRT || OS_WIN_STORE
}

void Launcher::registerUrlScheme() {
	base::Platform::RegisterUrlScheme(CustomSchemeDescriptor(this));
}

void Launcher::cleanupUrlScheme() {
	base::Platform::UnregisterUrlScheme(CustomSchemeDescriptor(this));
}

#ifdef WALLET_AUTOUPDATING_BUILD

void Launcher::startUpdateChecker() {
	_updateChecker = std::make_unique<Updater::Instance>(
		updaterSettings(),
		AppVersion);
	if (_updateChecker->readyToRestart()) {
		_restartingArguments = _arguments.mid(1);
		restartForUpdater();
	} else if (updateCheckerEnabled()) {
		_updateChecker->start(Updater::Instance::Start::Now);
	}
}

bool Launcher::restartingForUpdater() const {
	return _restartingForUpdater;
}

void Launcher::restartForUpdater() {
	Expects(_updateChecker != nullptr);

	_restartingForUpdater = true;
	Sandbox::Instance().quit();
}

not_null<Updater::Instance*> Launcher::updateChecker() {
	Expects(_updateChecker != nullptr);

	return _updateChecker.get();
}

void Launcher::setUpdateCheckerEnabled(bool enabled) {
	Expects(_updateChecker != nullptr);

	if (enabled) {
		QFile(updateCheckerDisabledFlagPath()).remove();
		_updateChecker->start(Updater::Instance::Start::Now);
	} else {
		auto f = QFile(updateCheckerDisabledFlagPath());
		f.open(QIODevice::WriteOnly);
		f.write("1", 1);
		_updateChecker->cancel();
	}
}

bool Launcher::updateCheckerEnabled() const {
	return !QFile(updateCheckerDisabledFlagPath()).exists();
}

QString Launcher::updateCheckerDisabledFlagPath() const {
	return _workingPath + "update_disabled";
}

Updater::Settings Launcher::updaterSettings() const {
	auto result = Updater::Settings();
	result.basePath = workingPath();
	result.url = "https://desktop-updates.ton.org/current";
	result.delayConstPart = kUpdaterDelayConstPart;
	result.delayRandPart = kUpdaterDelayRandPart;
	return result;
}

#endif // WALLET_AUTOUPDATING_BUILD

bool Launcher::canWorkInExecutablePath() const {
	const auto dataPath = _baseIntegration.executableDir() + "data";
	if (!QDir(dataPath).exists() && !QDir().mkpath(dataPath)) {
		return false;
	} else if (QFileInfo(dataPath + "/salt").exists()) {
		return true;
	}
	auto index = 0;
	while (true) {
		const auto temp = dataPath + "/temp" + QString::number(++index);
		auto file = QFile(temp);
		if (file.open(QIODevice::WriteOnly)) {
			file.close();
			file.remove();
			return true;
		} else if (!file.exists()) {
			return false;
		} else if (index == std::numeric_limits<int>::max()) {
			return false;
		}
	}
}

QString Launcher::checkPortablePath() {
	const auto portable = _baseIntegration.executableDir()
		+ "WalletForcePortable";
	return QDir(portable).exists() ? (portable + '/') : QString();
}

int Launcher::exec() {
	processArguments();

#ifdef WALLET_AUTOUPDATING_BUILD
	if (_action == Action::InstallUpdate) {
		return Updater::Install(_arguments, GetInfoForRegistry());
	}
#endif // WALLET_AUTOUPDATING_BUILD

	init();
	if (_action == Action::Cleanup) {
		cleanupInstallation();
		return 0;
	}
	setupScale();

	auto options = QJsonObject();
	const auto tempFontConfigPath = QStandardPaths::writableLocation(
		QStandardPaths::TempLocation
	) + "/fc-custom-1.conf";
	options.insert("custom_font_config_src", QString(":/fc/fc-custom.conf"));
	options.insert("custom_font_config_dst", tempFontConfigPath);
	Platform::Start(options);

	auto result = executeApplication();

	Platform::Finish();

#ifdef WALLET_AUTOUPDATING_BUILD
	auto restart = (_updateChecker && _restartingForUpdater)
		? _updateChecker->restarter()
		: nullptr;
	_updateChecker = nullptr;

	if (restart) {
		restart("Wallet", _restartingArguments);
	}
#endif // WALLET_AUTOUPDATING_BUILD

	return result;
}

void Launcher::cleanupInstallation() {
	cleanupUrlScheme();
}

void Launcher::setupScale() {
#ifdef Q_OS_MAC
	// macOS Retina display support is working fine, others are not.
	QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling, false);
#else // Q_OS_MAC
	QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling, true);
#endif // Q_OS_MAC
	Ui::DisableCustomScaling();
}

QStringList Launcher::readArguments(int argc, char *argv[]) const {
	Expects(argc >= 0);

	auto result = QStringList();
	result.reserve(argc);
	for (auto i = 0; i != argc; ++i) {
		result.push_back(base::FromUtf8Safe(argv[i]));
	}
	return result;
}

QString Launcher::argumentsString() const {
	return _arguments.join(' ');
}

QString Launcher::workingPath() const {
	return _workingPath;
}

QString Launcher::openedUrl() const {
	return _openedUrl;
}

void Launcher::initAppDataPath() {
	const auto path = QStandardPaths::writableLocation(
		QStandardPaths::AppDataLocation);
	const auto absolute = QDir(path).absolutePath();
	_appDataPath = absolute.endsWith('/') ? absolute : (absolute + '/');
}

void Launcher::processArguments() {
	_arguments = readArguments(_argc, _argv);

	auto nextUrl = false;
	for (const auto &argument : _arguments) {
		if (argument == "cleanup") {
			_action = Action::Cleanup;
		} else if (argument == "installupdate") {
			_action = Action::InstallUpdate;
			break;
		} else if (nextUrl) {
			_openedUrl = argument;
		} else if (argument == "--") {
			nextUrl = true;
		}
	}
}

int Launcher::executeApplication() {
	FilteredCommandLineArguments arguments(_argc, _argv);
	Sandbox sandbox(this, arguments.count(), arguments.values());
	Ui::MainQueueProcessor processor;
	base::ConcurrentTimerEnvironment environment;
	return sandbox.exec();
}

} // namespace Core
