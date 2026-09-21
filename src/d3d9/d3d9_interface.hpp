#pragma once

#include "com/com_object.hpp"
#include <d3d9.h>

namespace dxmt {

class D3D9Interface final : public ComObjectWithInitialRef<IDirect3D9Ex> {

public:
  D3D9Interface();

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) final;

  // IDirect3D9
  HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void *pInitializeFunction) final;
  UINT STDMETHODCALLTYPE GetAdapterCount() final;
  HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT Adapter, DWORD Flags,
                                                  D3DADAPTER_IDENTIFIER9 *pIdentifier) final;
  UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) final;
  HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode,
                                              D3DDISPLAYMODE *pMode) final;
  HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE *pMode) final;
  HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType,
                                             D3DFORMAT AdapterFormat,
                                             D3DFORMAT BackBufferFormat, BOOL bWindowed) final;
  HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType,
                                               D3DFORMAT AdapterFormat, DWORD Usage,
                                               D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) final;
  HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType,
                                                        D3DFORMAT SurfaceFormat, BOOL Windowed,
                                                        D3DMULTISAMPLE_TYPE MultiSampleType,
                                                        DWORD *pQualityLevels) final;
  HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType,
                                                    D3DFORMAT AdapterFormat,
                                                    D3DFORMAT RenderTargetFormat,
                                                    D3DFORMAT DepthStencilFormat) final;
  HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType,
                                                         D3DFORMAT SourceFormat,
                                                         D3DFORMAT TargetFormat) final;
  HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps) final;
  HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT Adapter) final;
  HRESULT STDMETHODCALLTYPE CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
                                          DWORD BehaviorFlags,
                                          D3DPRESENT_PARAMETERS *pPresentationParameters,
                                          IDirect3DDevice9 **ppReturnedDeviceInterface) final;

  // IDirect3D9Ex
  UINT STDMETHODCALLTYPE GetAdapterModeCountEx(UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter) final;
  HRESULT STDMETHODCALLTYPE EnumAdapterModesEx(UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter,
                                                UINT Mode, D3DDISPLAYMODEEX *pMode) final;
  HRESULT STDMETHODCALLTYPE GetAdapterDisplayModeEx(UINT Adapter, D3DDISPLAYMODEEX *pMode,
                                                     D3DDISPLAYROTATION *pRotation) final;
  HRESULT STDMETHODCALLTYPE CreateDeviceEx(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
                                            DWORD BehaviorFlags,
                                            D3DPRESENT_PARAMETERS *pPresentationParameters,
                                            D3DDISPLAYMODEEX *pFullscreenDisplayMode,
                                            IDirect3DDevice9Ex **ppReturnedDeviceInterface) final;
  HRESULT STDMETHODCALLTYPE GetAdapterLUID(UINT Adapter, LUID *pLUID) final;
};

/*
 * Adapter identity reported through IDirect3D9Ex::GetAdapterLUID.  Anything
 * stable and non-zero works; choosing a made-up value keeps us from claiming
 * to be a specific physical GPU.
 */
#define DXMT_D3D9_ADAPTER_LUID { 0x4d5345u, 0x0001u }

} // namespace dxmt
