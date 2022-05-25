// Copyright (C) Microsoft Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>

#include <memory>
#include <string>
#include <utility>

#include <shlwapi.h>
#include <shobjidl_core.h>
#include <wrl/module.h>
#include <wrl/implements.h>
#include <wrl/client.h>

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::InhibitRoOriginateError;
using Microsoft::WRL::Module;
using Microsoft::WRL::ModuleType;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

#define RETURN_IF_FAILED(expr)                                          \
  do {                                                                  \
    HRESULT hresult = (expr);                                           \
    if (FAILED(hresult)) {                                              \
      return hresult;                                                   \
    }                                                                   \
  } while (0)

namespace {

struct LocalAllocDeleter {
  void operator()(void* ptr) const { ::LocalFree(ptr); }
};

template <typename T>
std::unique_ptr<T, LocalAllocDeleter> TakeLocalAlloc(T*& ptr) {
  return std::unique_ptr<T, LocalAllocDeleter>(std::exchange(ptr, nullptr));
}

}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance,
                               DWORD reason,
                               LPVOID reserved) {
  switch (reason) {
    case DLL_PROCESS_ATTACH:
    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
      break;
  }

  return true;
}

class __declspec(uuid(DLL_UUID)) ExplorerCommandHandler final : public RuntimeClass<RuntimeClassFlags<ClassicCom | InhibitRoOriginateError>, IExplorerCommand> {
 public:
  // IExplorerCommand implementation:
  IFACEMETHODIMP GetTitle(IShellItemArray* items, PWSTR* name) {
    HKEY hkey;
    wchar_t value_w[1024];
    DWORD value_size_w = sizeof(value_w);
    DWORD access_flags = KEY_QUERY_VALUE;
    std::string full_registry_location(REGISTRY_LOCATION);
    RETURN_IF_FAILED(RegOpenKeyExA(HKEY_CLASSES_ROOT, full_registry_location.c_str(), 0, access_flags, &hkey));
    RETURN_IF_FAILED(RegGetValueW(hkey, nullptr, L"", RRF_RT_REG_SZ | REG_EXPAND_SZ | RRF_ZEROONFAILURE,
                                  NULL, reinterpret_cast<LPBYTE>(&value_w), &value_size_w));
    RETURN_IF_FAILED(RegCloseKey(hkey));
    return SHStrDup(value_w, name);
  }

  IFACEMETHODIMP GetIcon(IShellItemArray* items, PWSTR* icon) {
    HKEY hkey;
    wchar_t value_w[1024];
    DWORD value_size_w = sizeof(value_w);
    DWORD access_flags = KEY_QUERY_VALUE;
    std::string full_registry_location(REGISTRY_LOCATION);
    RETURN_IF_FAILED(RegOpenKeyExA(HKEY_CLASSES_ROOT, full_registry_location.c_str(), 0, access_flags, &hkey));
    RETURN_IF_FAILED(RegGetValueW(hkey, nullptr, L"Icon", RRF_RT_REG_SZ | REG_EXPAND_SZ | RRF_ZEROONFAILURE,
                                  NULL, reinterpret_cast<LPBYTE>(&value_w), &value_size_w));
    return SHStrDup(value_w, icon);
  }

  IFACEMETHODIMP GetToolTip(IShellItemArray* items, PWSTR* infoTip) {
    *infoTip = nullptr;
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetCanonicalName(GUID* guidCommandName) {
    *guidCommandName = GUID_NULL;
    return S_OK;
  }

  IFACEMETHODIMP GetState(IShellItemArray* items, BOOL okToBeSlow, EXPCMDSTATE* cmdState) {
    *cmdState = ECS_ENABLED;
    return S_OK;
  }

  IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) {
    *flags = ECF_DEFAULT;
    return S_OK;
  }

  IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** enumCommands) {
    *enumCommands = nullptr;
    return E_NOTIMPL;
  }

  IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx* bindCtx) {
    if (items) {
      DWORD count;
      RETURN_IF_FAILED(items->GetCount(&count));
      HKEY hkey;
			wchar_t value_w[1024];
			DWORD value_size_w = sizeof(value_w);
      DWORD access_flags = KEY_QUERY_VALUE;
      std::string full_registry_location(REGISTRY_LOCATION);
      full_registry_location += std::string("\\command");
      RETURN_IF_FAILED(RegOpenKeyExA(HKEY_CLASSES_ROOT, full_registry_location.c_str(), 0, access_flags, &hkey));
      RETURN_IF_FAILED(RegGetValueW(hkey, nullptr, L"", RRF_RT_REG_SZ | REG_EXPAND_SZ | RRF_ZEROONFAILURE,
                                    NULL, reinterpret_cast<LPBYTE>(&value_w), &value_size_w));
      RETURN_IF_FAILED(RegCloseKey(hkey));

      std::wstring paths;
      for (DWORD i = 0; i < count; ++i) {
        ComPtr<IShellItem> item;
        RETURN_IF_FAILED(items->GetItemAt(i, &item));
        LPWSTR path;
        RETURN_IF_FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
        paths += L" " + std::wstring(TakeLocalAlloc(path).get());
      }

      std::wstring command(value_w);
      command.replace(command.find(L"%1"), 2, paths);

      STARTUPINFOW startup_info = {sizeof(startup_info)};
      PROCESS_INFORMATION temp_process_info = {};
      RETURN_IF_FAILED(CreateProcess(
          nullptr, &command[0],
          nullptr /* lpProcessAttributes */, nullptr /* lpThreadAttributes */,
          FALSE /* bInheritHandles */, 0,
          nullptr /* lpEnvironment */, nullptr /* lpCurrentDirectory */,
          &startup_info, &temp_process_info));
      // Close thread and process handles of the new process.
      ::CloseHandle(temp_process_info.hProcess);
      ::CloseHandle(temp_process_info.hThread);
    }
    return S_OK;
  }
};

CoCreatableClass(ExplorerCommandHandler)
CoCreatableClassWrlCreatorMapInclude(ExplorerCommandHandler)

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
  if (ppv == nullptr)
    return E_POINTER;
  *ppv = nullptr;
  return Module<ModuleType::InProc>::GetModule().GetClassObject(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow(void) {
  return Module<ModuleType::InProc>::GetModule().GetObjectCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetActivationFactory(HSTRING activatableClassId,
                               IActivationFactory** factory) {
  return Module<ModuleType::InProc>::GetModule().GetActivationFactory(activatableClassId, factory);
}