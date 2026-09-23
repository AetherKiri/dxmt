#pragma once

#include "com/com_object.hpp"
#include "d3d9_debug_trace.hpp"
#include "d3d9_getdc.hpp"
#include "dxmt_texture.hpp"
#include <d3d9.h>
#include <cstdlib>
#include <cstring>
#include "log/log.hpp"

namespace dxmt {

class D3D9Device;

class D3D9Surface final : public ComObjectClamp<IDirect3DSurface9> {
public:
  // GPU-backed surface (render target, backbuffer, default-pool offscreen
  // plain surface).  The D3D format is only known to the caller, so it has to
  // be passed explicitly: GetDesc has to report what was requested, not the
  // Metal format the surface happens to share with other D3D formats.
  D3D9Surface(D3D9Device *device, Rc<Texture> texture, TextureViewKey viewKey,
              WMTPixelFormat mtlFormat = WMTPixelFormatInvalid,
              D3DFORMAT d3dFormat = D3DFMT_X8R8G8B8,
              DWORD usage = D3DUSAGE_RENDERTARGET);

  // System memory surface (for readback)
  D3D9Surface(D3D9Device *device, UINT width, UINT height, D3DFORMAT format, D3DPOOL pool);

  ~D3D9Surface();

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) final;

  // IDirect3DResource9
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) final;
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) final { return D3DERR_INVALIDCALL; }
  DWORD STDMETHODCALLTYPE SetPriority(DWORD) final { return 0; }
  DWORD STDMETHODCALLTYPE GetPriority() final { return 0; }
  void STDMETHODCALLTYPE PreLoad() final {}
  D3DRESOURCETYPE STDMETHODCALLTYPE GetType() final { return D3DRTYPE_SURFACE; }

  // IDirect3DSurface9
  HRESULT STDMETHODCALLTYPE GetContainer(REFIID, void **) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *pDesc) final;
  HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) final;
  HRESULT STDMETHODCALLTYPE UnlockRect() final;
  HRESULT STDMETHODCALLTYPE GetDC(HDC *phdc) final {
    UINT width = texture_ ? texture_->width() : width_;
    UINT height = texture_ ? texture_->height() : height_;
    return gdi_dc_.acquire(phdc, this, width, height, format_, texture_ != nullptr);
  }
  HRESULT STDMETHODCALLTYPE ReleaseDC(HDC hdc) final { return gdi_dc_.release(hdc); }

  Rc<Texture> &texture() { return texture_; }
  TextureViewKey viewKey() const { return viewKey_; }
  WMTPixelFormat mtlFormat() const { return mtl_format_; }
  D3DPOOL pool() const { return pool_; }
  void *sysMemData() const { return sys_mem_; }
  UINT pitch() const { return pitch_; }
  UINT sysWidth() const { return width_; }
  UINT sysHeight() const { return height_; }

private:
  D3D9Device *device_;

  // GPU surface
  Rc<Texture> texture_;
  TextureViewKey viewKey_ = 0;
  WMTPixelFormat mtl_format_ = WMTPixelFormatInvalid;
  DWORD usage_ = D3DUSAGE_RENDERTARGET;

  // System memory surface
  D3DPOOL pool_ = D3DPOOL_DEFAULT;
  D3DFORMAT format_ = D3DFMT_UNKNOWN;
  UINT width_ = 0;
  UINT height_ = 0;
  void *sys_mem_ = nullptr;
  UINT pitch_ = 0;
  bool locked_ = false;

  // GPU readback for lockable render targets
  void *readback_mem_ = nullptr;
  UINT readback_pitch_ = 0;

  // GDI DC support (see d3d9_getdc.hpp)
  D3D9SurfaceDC gdi_dc_;
};

} // namespace dxmt
