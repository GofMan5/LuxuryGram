// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#ifdef Q_OS_WIN

#include "luxury/utils/windows_utils.h"

#include "base/debug_log.h"
#include "platform/win/windows_app_user_model_id.h"

#include <ShlObj_core.h>

// An icon the user gave the shortcut in Explorer stays: only no icon, the
// executable's own or an .ico written to some tdata by us or by AyuGram
// follows the app icon setting.
bool ownsIcon(
		not_null<IShellLink*> link,
		const Platform::AppUserModelId::UniqueFileId &myid) {
	WCHAR location[MAX_PATH] = { 0 };
	auto index = 0;
	if (FAILED(link->GetIconLocation(location, MAX_PATH, &index))) {
		return false;
	} else if (!location[0]
		|| Platform::AppUserModelId::GetUniqueFileId(location) == myid) {
		return true;
	}
	const auto current = QFileInfo(QString::fromWCharArray(location));
	const auto name = current.fileName();
	return !current.dir().dirName().compare(u"tdata"_q, Qt::CaseInsensitive)
		&& name.endsWith(u".ico"_q, Qt::CaseInsensitive)
		&& (name.startsWith(u"LuxuryGram-"_q, Qt::CaseInsensitive)
			|| name.startsWith(u"AyuGram"_q, Qt::CaseInsensitive));
}

// Shortcuts are matched by name, and AyuGram or another LuxuryGram install
// uses the same names, so only a shortcut that launches this executable
// gets our icon.
bool processIcon(QString shortcut, QString iconPath) {
	if (!QFile::exists(shortcut)) {
		return false;
	}
	const auto myid = Platform::AppUserModelId::MyExecutablePathId();
	if (!myid) {
		return false;
	}

	IShellLink *pShellLink = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink,
								  nullptr,
								  CLSCTX_INPROC_SERVER,
								  IID_IShellLink,
								  (void**) &pShellLink);
	if (FAILED(hr)) {
		LOG(("LuxuryGram: could not create IShellLink, hr = %1.").arg(hr));
		return false;
	}
	const auto shellGuard = gsl::finally([&] { pShellLink->Release(); });

	IPersistFile *pPersistFile = nullptr;
	hr = pShellLink->QueryInterface(IID_IPersistFile, (void**) &pPersistFile);
	if (FAILED(hr)) {
		LOG(("LuxuryGram: IPersistFile is missing, hr = %1.").arg(hr));
		return false;
	}
	const auto persistGuard = gsl::finally([&] { pPersistFile->Release(); });

	const auto shortcutPath = shortcut.toStdWString();
	hr = pPersistFile->Load(shortcutPath.c_str(), STGM_READWRITE);
	if (FAILED(hr)) {
		LOG(("LuxuryGram: could not load shortcut %1, hr = %2.").arg(shortcut).arg(hr));
		return false;
	}

	WCHAR target[MAX_PATH] = { 0 };
	hr = pShellLink->GetPath(target, MAX_PATH, nullptr, 0);
	if (hr != S_OK
		|| Platform::AppUserModelId::GetUniqueFileId(target) != myid
		|| !ownsIcon(pShellLink, myid)) {
		return false;
	}

	hr = pShellLink->SetIconLocation(iconPath.toStdWString().c_str(), 0);
	if (FAILED(hr)) {
		LOG(("LuxuryGram: SetIconLocation failed for %1, hr = %2.").arg(shortcut).arg(hr));
		return false;
	}

	hr = pPersistFile->Save(shortcutPath.c_str(), TRUE);
	if (FAILED(hr)) {
		LOG(("LuxuryGram: could not save shortcut %1, hr = %2.").arg(shortcut).arg(hr));
		return false;
	}
	return true;
}

bool processLegacy(const QString &iconPath) {
	const auto appdata = QDir::fromNativeSeparators(qgetenv("APPDATA"));
	const auto taskbar = appdata + "/Microsoft/Internet Explorer/Quick Launch/User Pinned/TaskBar/";
	const auto shortcuts = {
		taskbar + u"LuxuryGram Desktop.lnk"_q,
		taskbar + u"LuxuryGram.lnk"_q,
		taskbar + u"AyuGram Desktop.lnk"_q,
		taskbar + u"AyuGram.lnk"_q,
	};
	auto changed = false;
	for (const auto &shortcut : shortcuts) {
		changed |= processIcon(shortcut, iconPath);
	}
	return changed;
}

bool processNewPinned(const QString &iconPath) {
	const auto hrInit = CoInitialize(0);
	if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE) {
		return false;
	}
	const auto coGuard = gsl::finally([&]
	{
		if (SUCCEEDED(hrInit)) {
			CoUninitialize();
		}
	});

	const auto path = Platform::AppUserModelId::PinnedIconsPath();
	const auto native = QDir::toNativeSeparators(path).toStdWString();

	const auto srcid = Platform::AppUserModelId::MyExecutablePathId();
	if (!srcid) {
		return false;
	}

	WIN32_FIND_DATA findData;
	HANDLE findHandle = FindFirstFileEx(
		(native + L"*.lnk").c_str(),
		FindExInfoStandard,
		&findData,
		FindExSearchNameMatch,
		0,
		0);
	if (findHandle == INVALID_HANDLE_VALUE) {
		return false;
	}
	const auto closeGuard = gsl::finally([&] { FindClose(findHandle); });

	auto changed = false;
	do {
		std::wstring fname = native + findData.cFileName;
		const auto filePath = QString::fromStdWString(fname);
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			continue;
		}
		changed |= processIcon(filePath, iconPath);
	} while (FindNextFile(findHandle, &findData));

	const auto errorCode = GetLastError();
	if (errorCode && errorCode != ERROR_NO_MORE_FILES) {
		LOG(("LuxuryGram: enumerating pinned shortcuts failed, error %1.").arg(errorCode));
	}
	return changed;
}

bool processNewShortcuts(const QString &iconPath) {
	const auto path = Platform::AppUserModelId::systemShortcutPath();
	if (path.isEmpty()) {
		return false;
	}

	const auto shortcuts = {
		path + u"LuxuryGram Desktop/LuxuryGram.lnk"_q,
		path + u"LuxuryGram/LuxuryGram.lnk"_q,
		path + u"LuxuryGram.lnk"_q,
		path + u"AyuGram Desktop/AyuGram.lnk"_q,
		path + u"AyuGram/AyuGram.lnk"_q,
		path + u"AyuGram.lnk"_q,
	};
	auto changed = false;
	for (const auto &shortcut : shortcuts) {
		const auto native = QDir::toNativeSeparators(shortcut).toStdWString();

		const auto attributes = GetFileAttributes(native.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES) {
			continue;
		}

		changed |= processIcon(QString::fromStdWString(native), iconPath);
	}
	return changed;
}

void reloadAppIconFromTaskBar(const QString &iconPath) {
	const auto pinned = processNewPinned(iconPath);
	const auto shortcuts = processNewShortcuts(iconPath);
	const auto legacy = processLegacy(iconPath);

	// Makes Explorer drop its whole icon cache and redraw every icon on the
	// desktop, so only when one of our shortcuts really got a new icon.
	if (pinned || shortcuts || legacy) {
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	}
}

#endif
