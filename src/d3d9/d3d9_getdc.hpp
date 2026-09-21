#pragma once

#include "log/log.hpp"
#include <d3d9.h>
#include <windows.h>
#include <cstring>

namespace dxmt {

/*
 * Shared IDirect3DSurface9::GetDC / ReleaseDC support.
 *
 * GDI drawing into a Direct3D surface is how plenty of 2D-heavy titles paint
 * text, cursor overlays and UI: they ask for a DC, draw with GDI, and release
 * it.  The DC is backed by a top-down 32-bit DIB section, whose memory layout
 * is exactly D3D9's A8R8G8B8/X8R8G8B8, so a release copies the pixels straight
 * back into the surface and the next texture upload picks them up.
 *
 * Only the 32-bit formats GDI can represent are accepted, which is what the
 * D3D9 documentation requires; everything else reports D3DERR_INVALIDCALL the
 * way a driver without a GDI-compatible path does.
 */
class D3D9SurfaceDC {
public:
  ~D3D9SurfaceDC() { destroy(); }

  bool busy() const { return dc_ != nullptr; }

  HRESULT acquire(HDC *phdc, IDirect3DSurface9 *surface, UINT width, UINT height,
                  D3DFORMAT format, bool gpu_backed) {
    if (!phdc || !surface || dc_)
      return D3DERR_INVALIDCALL;
    if (!isGdiFormat(format)) {
      warnOnce("D3D9: GetDC on format ", (int)format, " is not supported");
      return D3DERR_INVALIDCALL;
    }
    if (!width || !height)
      return D3DERR_INVALIDCALL;

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
      return D3DERR_INVALIDCALL;

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = (LONG)width;
    info.bmiHeader.biHeight = -(LONG)height; // top-down, like D3D9
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
      DeleteDC(dc);
      return D3DERR_INVALIDCALL;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);

    D3DLOCKED_RECT locked = {};
    if (FAILED(surface->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
      SelectObject(dc, old);
      DeleteObject(bitmap);
      DeleteDC(dc);
      return D3DERR_INVALIDCALL;
    }

    for (UINT y = 0; y < height; y++) {
      std::memcpy((uint8_t *)bits + (size_t)y * width * 4,
                  (const uint8_t *)locked.pBits + (size_t)y * locked.Pitch,
                  (size_t)width * 4);
    }
    surface->UnlockRect();

    dc_ = dc;
    bitmap_ = bitmap;
    old_bitmap_ = old;
    bits_ = bits;
    surface_ = surface;
    width_ = width;
    height_ = height;
    writeback_ = !gpu_backed;

    /*
     * A GPU-backed surface cannot be written back without a staging texture of
     * its own, so the DC is read-only for those; say so once instead of
     * silently dropping the pixels the title draws.
     */
    if (gpu_backed)
      warnOnce("D3D9: GetDC on a GPU-backed surface is read-only");

    *phdc = dc;
    return S_OK;
  }

  HRESULT release(HDC hdc) {
    if (!dc_ || hdc != dc_)
      return D3DERR_INVALIDCALL;

    if (writeback_ && surface_) {
      GdiFlush();
      D3DLOCKED_RECT locked = {};
      if (SUCCEEDED(surface_->LockRect(&locked, nullptr, 0))) {
        for (UINT y = 0; y < height_; y++) {
          std::memcpy((uint8_t *)locked.pBits + (size_t)y * locked.Pitch,
                      (const uint8_t *)bits_ + (size_t)y * width_ * 4,
                      (size_t)width_ * 4);
        }
        surface_->UnlockRect();
      }
    }

    destroy();
    return S_OK;
  }

private:
  template <typename A, typename B>
  void warnOnce(const char *message, A a, B b) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      Logger::warn(str::format(message, a, b));
    }
  }
  void warnOnce(const char *message) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      Logger::warn(message);
    }
  }

  static bool isGdiFormat(D3DFORMAT format) {
    switch (format) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
      return true;
    default:
      return false;
    }
  }

  void destroy() {
    if (dc_) {
      if (old_bitmap_)
        SelectObject(dc_, old_bitmap_);
      if (bitmap_)
        DeleteObject(bitmap_);
      DeleteDC(dc_);
    }
    dc_ = nullptr;
    bitmap_ = nullptr;
    old_bitmap_ = nullptr;
    bits_ = nullptr;
    surface_ = nullptr;
  }

  HDC dc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ old_bitmap_ = nullptr;
  void *bits_ = nullptr;
  IDirect3DSurface9 *surface_ = nullptr;
  UINT width_ = 0;
  UINT height_ = 0;
  bool writeback_ = false;
};

} // namespace dxmt
