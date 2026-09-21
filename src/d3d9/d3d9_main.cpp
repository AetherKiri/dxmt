
#include "d3d9_interface.hpp"
#include "log/log.hpp"

namespace dxmt {
Logger Logger::s_instance("d3d9.log");

extern "C" IDirect3D9 *WINAPI Direct3DCreate9(UINT SDKVersion) {
  Logger::info("Direct3DCreate9 called");
  /*
   * Diagnosis: prove which module is loaded and whether the guest sees the
   * host environment (the entry trace in the device is gated on it).
   */
  {
    char module_path[MAX_PATH] = "?";
    HMODULE self = GetModuleHandleA("d3d9.dll");
    if (self)
      GetModuleFileNameA(self, module_path, sizeof(module_path));
    fprintf(stderr, "D3D9CONF module=%s conf_trace=%s\n", module_path,
            getenv("MSE_D3D9_CONF_TRACE") ? "yes" : "no");
    fflush(stderr);
  }
  return new D3D9Interface();
}

/*
 * Identity marker: lets a test (or a log) prove that the running Direct3D 9
 * module is this driver and not Wine's builtin, which otherwise looks
 * identical from the outside.
 */
#define MSE_D3D9_BUILD_TAG 0x4d534544u /* "MSED" */

extern "C" unsigned long WINAPI MSE_D3D9BuildTag(void) {
  return MSE_D3D9_BUILD_TAG;
}

extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex **ppDirect3D9Ex) {
  Logger::info("Direct3DCreate9Ex called");
  if (!ppDirect3D9Ex)
    return D3DERR_INVALIDCALL;
  *ppDirect3D9Ex = nullptr;

  /*
   * The layer implements the full IDirect3D9Ex surface on the same object, so
   * a title that asks for the Ex entry point gets a device it can use with
   * either interface.  Behaviour is the plain D3D9 one plus the Ex methods.
   */
  if (SDKVersion != D3D_SDK_VERSION && SDKVersion != 0) {
    Logger::warn(str::format("Direct3DCreate9Ex: unexpected SDK version ", SDKVersion));
  }
  *ppDirect3D9Ex = static_cast<IDirect3D9Ex *>(new D3D9Interface());
  return S_OK;
}

/*
 * D3D9On12 needs a D3D12 device underneath; there is none here, so report
 * "not supported" the way a driver without the D3D9On12 bridge does.
 */
extern "C" HRESULT WINAPI Direct3DCreate9On12(UINT, void *, UINT) {
  static bool warned = false;
  if (!warned) {
    warned = true;
    Logger::warn("Direct3DCreate9On12: not supported");
  }
  return E_NOTIMPL;
}

/*
 * PIX / PIX-like instrumentation.  Titles built with the D3DPERF macros import
 * these, and a missing export makes the whole process fail to load; they only
 * capture GPU annotations we do not consume, so they are real no-ops here.
 */
extern "C" int WINAPI D3DPERF_BeginEvent(D3DCOLOR, LPCWSTR) { return 0; }
extern "C" int WINAPI D3DPERF_EndEvent() { return 0; }
extern "C" void WINAPI D3DPERF_SetMarker(D3DCOLOR, LPCWSTR) {}
extern "C" void WINAPI D3DPERF_SetRegion(D3DCOLOR, LPCWSTR) {}
extern "C" void WINAPI D3DPERF_SetOptions(DWORD) {}
extern "C" BOOL WINAPI D3DPERF_QueryRepeatFrame() { return FALSE; }
extern "C" DWORD WINAPI D3DPERF_GetStatus() { return 0; }

extern "C" void WINAPI DebugSetMute() {}
extern "C" int WINAPI DebugSetLevel() { return 0; }
extern "C" int WINAPI Direct3D9EnableMaximizedWindowedModeShim(UINT) { return 0; }

/*
 * Processor-specific geometry pipeline hooks (P3/3DNow! software TnL).  The
 * layer never uses a PSGP path, so these are the same no-ops Wine ships.
 */
extern "C" void WINAPI PSGPError(void *, int, UINT) {}
extern "C" void WINAPI PSGPSampleTexture(void *, UINT, float (*const)[4], UINT,
                                        float (*const)[4]) {}

/*
 * The shader validator is a debug-build facility (it validates shader bytecode
 * as it is created).  Reporting "no validator" is what a release driver does;
 * nothing in a shipping title calls it.
 */
extern "C" IUnknown *WINAPI Direct3DShaderValidatorCreate9() { return nullptr; }

} // namespace dxmt

#ifndef DXMT_NATIVE

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  if (reason != DLL_PROCESS_ATTACH)
    return TRUE;

  DisableThreadLibraryCalls(instance);
  return TRUE;
}

#endif
