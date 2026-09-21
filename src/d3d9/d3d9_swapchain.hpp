#pragma once

#include "com/com_object.hpp"
#include "com/com_pointer.hpp"
#include "d3d9_surface.hpp"
#include "dxmt_texture.hpp"
#include <d3d9.h>

namespace dxmt {

class D3D9Device;

/*
 * IDirect3DSwapChain9.
 *
 * The implicit swap chain (index 0) shares the device's back buffer, so
 * GetBackBuffer(0, ...) returns the same surface the device renders into and
 * Present() is the device's present path.
 *
 * Additional swap chains own a back buffer of their own.  Present() copies
 * that back buffer into the device's window and presents it; a title that
 * presents a second chain to a *different* window only gets the copy into the
 * focus window, which is the honest limit of a single-layer presenter.
 */
class D3D9SwapChain final : public ComObjectClamp<IDirect3DSwapChain9> {
public:
  D3D9SwapChain(D3D9Device *device, const D3DPRESENT_PARAMETERS &params,
                Rc<Texture> backbuffer, TextureViewKey view,
                Com<D3D9Surface> surface, bool implicit)
      : device_(device), params_(params), backbuffer_(std::move(backbuffer)),
        view_(view), surface_(std::move(surface)), implicit_(implicit) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) final {
    if (!ppvObj)
      return E_POINTER;
    *ppvObj = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DSwapChain9)) {
      *ppvObj = ref(this);
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE Present(const RECT *pSourceRect, const RECT *pDestRect,
                                    HWND hDestWindowOverride, const RGNDATA *pDirtyRegion,
                                    DWORD dwFlags) final;
  HRESULT STDMETHODCALLTYPE GetFrontBufferData(IDirect3DSurface9 *pDestSurface) final;
  HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT iBackBuffer, D3DBACKBUFFER_TYPE Type,
                                          IDirect3DSurface9 **ppBackBuffer) final;
  HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS *pRasterStatus) final;
  HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE *pMode) final;
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) final;
  HRESULT STDMETHODCALLTYPE GetPresentParameters(D3DPRESENT_PARAMETERS *pPresentationParameters) final;

  Rc<Texture> &backbuffer() { return backbuffer_; }
  TextureViewKey view() const { return view_; }
  D3D9Surface *surface() const { return surface_.ptr(); }
  bool implicit() const { return implicit_; }

  void setBackbuffer(Rc<Texture> backbuffer, TextureViewKey view,
                     Com<D3D9Surface> surface) {
    backbuffer_ = std::move(backbuffer);
    view_ = view;
    surface_ = std::move(surface);
  }
  void setPresentParameters(const D3DPRESENT_PARAMETERS &params) { params_ = params; }

private:
  D3D9Device *device_;
  D3DPRESENT_PARAMETERS params_ = {};
  Rc<Texture> backbuffer_;
  TextureViewKey view_ = 0;
  Com<D3D9Surface> surface_;
  bool implicit_ = false;
};

} // namespace dxmt
