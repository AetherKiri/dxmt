#pragma once

#include "com/com_object.hpp"
#include "dxmt_texture.hpp"
#include "d3d9_debug_trace.hpp"
#include "d3d9_format.hpp"
#include "log/log.hpp"
#include <d3d9.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace dxmt {

class D3D9Device;
class D3D9TextureSurface;

class D3D9Texture2D final : public ComObjectClamp<IDirect3DTexture9> {
public:
  D3D9Texture2D(D3D9Device *device, UINT width, UINT height, UINT levels,
                 D3DFORMAT format, Rc<Texture> texture, TextureViewKey viewKey)
      : device_(device), width_(width), height_(height), format_(format),
        texture_(std::move(texture)), viewKey_(viewKey) {
    if (levels == 0)
      levels = (UINT)std::floor(std::log2((double)std::max(width, height))) + 1;
    levelCount_ = levels;

    // Allocate per-level staging data
    UINT mipW = width, mipH = height;
    for (UINT i = 0; i < levelCount_; i++) {
      MipLevel mip;
      mip.width = mipW;
      mip.height = mipH;
      mip.pitch = D3D9FormatPitch(format, mipW);
      mip.dataSize = D3D9FormatMipSize(format, mipW, mipH);
      mip.data = std::malloc(mip.dataSize);
      std::memset(mip.data, 0, mip.dataSize);
      mip.dirty = false;
      mips_.push_back(mip);
      mipW = std::max(1u, mipW / 2);
      mipH = std::max(1u, mipH / 2);
    }
  }

  ~D3D9Texture2D() {
    for (auto &mip : mips_) {
      if (mip.data) std::free(mip.data);
    }
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) final {
    if (!ppvObj) return E_POINTER;
    *ppvObj = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DResource9) ||
        riid == __uuidof(IDirect3DBaseTexture9) || riid == __uuidof(IDirect3DTexture9)) {
      *ppvObj = ref(this);
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  // IDirect3DResource9
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) final { return D3DERR_INVALIDCALL; }
  DWORD STDMETHODCALLTYPE SetPriority(DWORD) final { return 0; }
  DWORD STDMETHODCALLTYPE GetPriority() final { return 0; }
  void STDMETHODCALLTYPE PreLoad() final {}
  D3DRESOURCETYPE STDMETHODCALLTYPE GetType() final { return D3DRTYPE_TEXTURE; }

  // IDirect3DBaseTexture9
  DWORD STDMETHODCALLTYPE SetLOD(DWORD) final { return 0; }
  DWORD STDMETHODCALLTYPE GetLOD() final { return 0; }
  DWORD STDMETHODCALLTYPE GetLevelCount() final { return levelCount_; }
  HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) final { return S_OK; }
  D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() final { return D3DTEXF_NONE; }
  void STDMETHODCALLTYPE GenerateMipSubLevels() final {}

  // IDirect3DTexture9
  HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) final {
    if (Level >= levelCount_ || !pDesc) return D3DERR_INVALIDCALL;
    pDesc->Format = format_;
    pDesc->Type = D3DRTYPE_SURFACE;
    pDesc->Usage = 0;
    pDesc->Pool = D3DPOOL_MANAGED;
    pDesc->MultiSampleType = D3DMULTISAMPLE_NONE;
    pDesc->MultiSampleQuality = 0;
    pDesc->Width = mips_[Level].width;
    pDesc->Height = mips_[Level].height;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurfaceLevel) final;

  HRESULT STDMETHODCALLTYPE LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) final {
    if (Level >= levelCount_ || !pLockedRect) return D3DERR_INVALIDCALL;
    if (DebugTraceBudget("MSE_TRACE_TEX") > 0) {
      DebugTraceBudget("MSE_TRACE_TEX")--;
      Logger::info(str::format("#", DebugTraceSeq(), " Lock tex=", (void *)this, " lv=", Level, " ",
                               width_, "x", height_, " flags=0x", (unsigned)Flags, " rect=",
                               pRect ? (int)pRect->left : -1, ",", pRect ? (int)pRect->top : -1, ",",
                               pRect ? (int)pRect->right : -1, ",",
                               pRect ? (int)pRect->bottom : -1, " caller=",
                               __builtin_return_address(0), "+",
                               __builtin_return_address(1)));
    }
    auto &mip = mips_[Level];
    pLockedRect->Pitch = mip.pitch;
    if (pRect) {
      // Return pointer to the sub-rect origin within the mip buffer
      UINT bytesPerPixel = mip.pitch / mip.width;
      pLockedRect->pBits = static_cast<uint8_t *>(mip.data)
        + pRect->top * mip.pitch + pRect->left * bytesPerPixel;
    } else {
      pLockedRect->pBits = mip.data;
    }
    if (!(Flags & D3DLOCK_NO_DIRTY_UPDATE))
      mip.dirty = true;
    if (getenv("MSE_DEBUG_POISON"))
      memset(mip.data, 0xCD, mip.dataSize);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE UnlockRect(UINT Level) final {
    if (Level >= levelCount_) return D3DERR_INVALIDCALL;
    if (DebugTraceBudget("MSE_TRACE_TEX") > 0) {
      DebugTraceBudget("MSE_TRACE_TEX")--;
      char stats[256];
      debugStats(stats, sizeof(stats));
      Logger::info(str::format("#", DebugTraceSeq(), " Unlock tex=", (void *)this, " lv=", Level,
                               " ", stats, " caller=", __builtin_return_address(0)));
    }
    if (getenv("MSE_DEBUG_POISON")) {
      // The buffer was filled with 0xCD at lock time, so any deviation is a
      // write performed by the guest.  Report only uploads that carried real
      // pixels; a long run with no report means the title never decoded.
      auto &mip = mips_[Level];
      const uint8_t *p = static_cast<const uint8_t *>(mip.data);
      size_t changed = 0, nz = 0;
      for (size_t i = 0; i < mip.dataSize; i++) {
        if (p[i] == 0xCD)
          continue; // untouched: still the poison pattern
        changed++;
        if (p[i])
          nz++;
      }
      static uint64_t total = 0, withPixel = 0, best = 0;
      total++;
      if (nz > best) best = nz;
      if (nz) {
        withPixel++;
        if (withPixel <= 12) {
          char path[128];
          snprintf(path, sizeof(path), "Z:/tmp/mse-px-%.0f.bin", (double)withPixel);
          if (FILE *fp = fopen(path, "wb")) {
            fwrite(p, 1, mip.dataSize, fp);
            fclose(fp);
          }
          Logger::info(str::format("D3D9POISON: upload with pixels #", (double)withPixel, " tex=",
                                   (void *)this, " ", width_, "x", height_, " changed=",
                                   (double)changed, "/", (double)mip.dataSize, " nz=", (double)nz,
                                   " head=", (int)p[0], ",", (int)p[1], ",", (int)p[2], ",", (int)p[3],
                                   " dumped=", path));
        }
      } else if ((total % 500) == 0) {
        Logger::info(str::format("D3D9POISON: summary locks=", (double)total, " withPixels=",
                                 (double)withPixel, " bestNz=", (double)best));
      }
    }
    return S_OK;
  }

  // D3D9 lets a title update a resource without a plain lock: Lock() with
  // D3DLOCK_NO_DIRTY_UPDATE followed by AddDirtyRect().  Ignoring the dirty
  // rect leaves the GPU copy stale, which is exactly what kept A7-3's title
  // tiles black (the engine uploads its images through that pair).
  HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT *pDirtyRect) final {
    if (DebugTraceBudget("MSE_TRACE_DIRTY") > 0) {
      DebugTraceBudget("MSE_TRACE_DIRTY")--;
      Logger::info(str::format("D3D9DIRTY: AddDirtyRect tex=", (void *)this, " ",
                               pDirtyRect ? (int)pDirtyRect->left : -1, ",",
                               pDirtyRect ? (int)pDirtyRect->top : -1, ",",
                               pDirtyRect ? (int)pDirtyRect->right : -1, ",",
                               pDirtyRect ? (int)pDirtyRect->bottom : -1));
    }
    for (auto &mip : mips_)
      mip.dirty = true;
    return S_OK;
  }

  // Internal accessors
  Rc<Texture> &texture() { return texture_; }
  TextureViewKey viewKey() const { return viewKey_; }
  UINT width() const { return width_; }
  UINT height() const { return height_; }
  D3DFORMAT format() const { return format_; }
  UINT levelCount() const { return levelCount_; }

  // Debug: number of non-zero bytes in level 0 (cheap scan of the CPU staging
  // copy, used to tell "the title uploaded real pixels" from "it stayed black").
  size_t nonZeroBytes() const {
    if (mips_.empty())
      return 0;
    auto &mip = mips_[0];
    const uint8_t *p = static_cast<const uint8_t *>(mip.data);
    size_t nz = 0;
    for (size_t i = 0; i < mip.dataSize; i++)
      if (p[i]) nz++;
    return nz;
  }

  // Debug: summarise the CPU-side staging data for level 0.  Distinguishes a
  // texture the title never filled in from a broken sampling path.
  void debugStats(char *out, size_t outSize) const {
    if (mips_.empty()) {
      snprintf(out, outSize, "no-mips");
      return;
    }
    auto &mip = mips_[0];
    const uint8_t *p = static_cast<const uint8_t *>(mip.data);
    size_t nz = 0;
    for (size_t i = 0; i < mip.dataSize; i++)
      if (p[i]) nz++;
    char hex[64] = {};
    size_t hp = 0;
    for (size_t i = 0; i < 16 && i < mip.dataSize && hp + 3 < sizeof(hex); i++)
      hp += (size_t)snprintf(hex + hp, sizeof(hex) - hp, "%02x", p[i]);
    snprintf(out, outSize, "fmt=%d %ux%u lv=%u pitch=%u nz=%zu/%zu head=%s",
             (int)format_, width_, height_, levelCount_, mip.pitch, nz,
             mip.dataSize, hex);
  }

  void markAllDirty() {
    for (auto &mip : mips_)
      mip.dirty = true;
  }

  bool isAnyDirty() const {
    for (auto &mip : mips_)
      if (mip.dirty) return true;
    return false;
  }

  void uploadDirtyLevels(WMT::Texture mtlTexture) {
    bool compressed = IsCompressedFormat(format_);
    for (UINT i = 0; i < levelCount_; i++) {
      auto &mip = mips_[i];
      if (!mip.dirty) continue;

      WMTOrigin origin = {0, 0, 0};
      WMTSize size = {mip.width, mip.height, 1};
      if (compressed) {
        uint32_t rows = (mip.height + 3) / 4;
        mtlTexture.replaceRegion(origin, size, i, 0, mip.data, mip.pitch, mip.pitch * rows);
      } else if (format_ == D3DFMT_A4R4G4B4) {
        // Convert A4R4G4B4 (2bpp) → BGRA8 (4bpp) for upload
        uint32_t gpuPitch = mip.width * 4;
        std::vector<uint32_t> converted(mip.width * mip.height);
        auto *src16 = (const uint16_t *)mip.data;
        for (UINT row = 0; row < mip.height; row++) {
          for (UINT col = 0; col < mip.width; col++) {
            uint16_t px = src16[row * (mip.pitch / 2) + col];
            // D3D9 A4R4G4B4: A(15-12) R(11-8) G(7-4) B(3-0)
            uint8_t a4 = (px >> 12) & 0xF;
            uint8_t r4 = (px >> 8) & 0xF;
            uint8_t g4 = (px >> 4) & 0xF;
            uint8_t b4 = px & 0xF;
            // Expand 4-bit to 8-bit: val * 17 = val * 255 / 15
            // Pack as BGRA8 (little-endian: bytes B, G, R, A)
            converted[row * mip.width + col] =
              (uint32_t)(b4 * 17) |
              ((uint32_t)(g4 * 17) << 8) |
              ((uint32_t)(r4 * 17) << 16) |
              ((uint32_t)(a4 * 17) << 24);
          }
        }
        mtlTexture.replaceRegion(origin, size, i, 0, converted.data(), gpuPitch, 0);
      } else {
        mtlTexture.replaceRegion(origin, size, i, 0, mip.data, mip.pitch, 0);
      }
      mip.dirty = false;
    }
  }

  // Staged upload: copies mip data to a staging buffer and emits a blit command
  // into the current command chunk. This avoids the replaceRegion race where
  // the CPU overwrites shared texture data while the GPU still reads it.
  void uploadDirtyLevelsStaged(Rc<Texture> &gpuTex, dxmt::CommandQueue &queue) {
    bool compressed = IsCompressedFormat(format_);
    for (UINT i = 0; i < levelCount_; i++) {
      auto &mip = mips_[i];
      if (!mip.dirty) continue;

      const void *srcData = mip.data;
      UINT srcPitch = mip.pitch;
      UINT uploadHeight = mip.height;
      UINT uploadPitch = srcPitch;

      // Handle format conversions
      std::vector<uint32_t> convertedBuf;
      if (format_ == D3DFMT_A4R4G4B4) {
        uploadPitch = mip.width * 4;
        convertedBuf.resize(mip.width * mip.height);
        auto *src16 = (const uint16_t *)mip.data;
        for (UINT row = 0; row < mip.height; row++) {
          for (UINT col = 0; col < mip.width; col++) {
            uint16_t px = src16[row * (srcPitch / 2) + col];
            uint8_t a4 = (px >> 12) & 0xF;
            uint8_t r4 = (px >> 8) & 0xF;
            uint8_t g4 = (px >> 4) & 0xF;
            uint8_t b4 = px & 0xF;
            convertedBuf[row * mip.width + col] =
              (uint32_t)(b4 * 17) | ((uint32_t)(g4 * 17) << 8) |
              ((uint32_t)(r4 * 17) << 16) | ((uint32_t)(a4 * 17) << 24);
          }
        }
        srcData = convertedBuf.data();
        srcPitch = uploadPitch;
      }

      if (compressed)
        uploadHeight = (mip.height + 3) / 4;

      size_t uploadSize = (size_t)uploadPitch * uploadHeight;
      auto staging = queue.AllocateTransientBuffer(uploadSize, 16);
      if (!staging.cpu_ptr) {
        WARN("D3D9: transient upload allocation failed for level ", i);
        return;
      }
      std::memcpy(staging.cpu_ptr, srcData, uploadSize);
      if (getenv("MSE_DEBUG_TEXCHECKER")) {
        uint8_t *dst = (uint8_t *)staging.cpu_ptr;
        for (UINT row = 0; row < uploadHeight; row++) {
          for (UINT col = 0; col < uploadPitch / 4; col++) {
            uint8_t *px = dst + (size_t)row * uploadPitch + (size_t)col * 4;
            bool on = ((row / 8) ^ (col / 8)) & 1;
            px[0] = on ? 0x00 : 0xff; // B
            px[1] = on ? 0xff : 0x00; // G
            px[2] = on ? 0x00 : 0xff; // R
            px[3] = 0xff;             // A
          }
        }
      }

      if (DebugTraceBudget("MSE_TRACE_UPLOAD") > 0) {
        DebugTraceBudget("MSE_TRACE_UPLOAD")--;
        const uint8_t *b = static_cast<const uint8_t *>(srcData);
        size_t nz = 0;
        for (size_t i = 0; i < uploadSize; i++)
          if (b[i]) nz++;
        Logger::info(str::format("D3D9UP: upload tex=", (void *)this, " ", mip.width, "x", mip.height,
                                 " lv=", i, " pitch=", uploadPitch, " size=", (double)uploadSize,
                                 " nz=", (double)nz, " head=", (int)b[0], ",", (int)b[1], ",",
                                 (int)b[2], ",", (int)b[3]));
      }

      auto chunk = queue.CurrentChunk();
      chunk->emitcc([
        tex = gpuTex,
        stagingBuf = staging.buffer.handle,
        stagingOff = (uint64_t)staging.offset,
        w = mip.width, h = mip.height,
        pitch = uploadPitch, level = i
      ](ArgumentEncodingContext &ctx) mutable {
        ctx.startBlitPass();
        auto dstHandle = ctx.access(tex, (unsigned)level, 0u, DXMT_ENCODER_RESOURCE_ACESS_WRITE);
        auto &blitCmd = ctx.encodeBlitCommand<wmtcmd_blit_copy_from_buffer_to_texture>();
        blitCmd.type = WMTBlitCommandCopyFromBufferToTexture;
        blitCmd.src = stagingBuf;
        blitCmd.src_offset = stagingOff;
        blitCmd.bytes_per_row = pitch;
        blitCmd.bytes_per_image = 0;
        blitCmd.dst = dstHandle;
        blitCmd.level = level;
        blitCmd.slice = 0;
        blitCmd.origin = {0, 0, 0};
        blitCmd.size = {w, h, 1};
        ctx.endPass();
      });

      mip.dirty = false;
    }
  }

  // Legacy accessors for backward compat with single-level path
  bool isDirty() const { return isAnyDirty(); }
  void clearDirty() {
    for (auto &mip : mips_) mip.dirty = false;
  }
  const void *data() const { return mips_[0].data; }
  UINT pitch() const { return mips_[0].pitch; }

private:
  struct MipLevel {
    UINT width;
    UINT height;
    UINT pitch;
    size_t dataSize;
    void *data = nullptr;
    bool dirty = false;
  };

  D3D9Device *device_;
  UINT width_;
  UINT height_;
  D3DFORMAT format_;
  UINT levelCount_;
  std::vector<MipLevel> mips_;

  Rc<Texture> texture_;
  TextureViewKey viewKey_;
};

// Lightweight surface wrapper for GetSurfaceLevel — delegates Lock/Unlock to parent texture mip
class D3D9TextureSurface final : public ComObjectClamp<IDirect3DSurface9> {
public:
  D3D9TextureSurface(D3D9Texture2D *parent, UINT level)
      : parent_(parent), level_(level) {
    parent_->AddRef();
  }
  ~D3D9TextureSurface() {
    if (parent_) parent_->Release();
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) final {
    if (!ppvObj) return E_POINTER;
    *ppvObj = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DResource9) ||
        riid == __uuidof(IDirect3DSurface9)) {
      *ppvObj = ref(this);
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) final { return D3DERR_INVALIDCALL; }
  DWORD STDMETHODCALLTYPE SetPriority(DWORD) final { return 0; }
  DWORD STDMETHODCALLTYPE GetPriority() final { return 0; }
  void STDMETHODCALLTYPE PreLoad() final {}
  D3DRESOURCETYPE STDMETHODCALLTYPE GetType() final { return D3DRTYPE_SURFACE; }

  HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **ppContainer) final {
    if (!ppContainer) return E_POINTER;
    return parent_->QueryInterface(riid, ppContainer);
  }

  HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *pDesc) final {
    return parent_->GetLevelDesc(level_, pDesc);
  }

  HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) final {
    return parent_->LockRect(level_, pLockedRect, pRect, Flags);
  }

  HRESULT STDMETHODCALLTYPE UnlockRect() final {
    return parent_->UnlockRect(level_);
  }

  HRESULT STDMETHODCALLTYPE GetDC(HDC *) final {
    if (DebugTraceBudget("MSE_TRACE_FILL") > 0) {
      DebugTraceBudget("MSE_TRACE_FILL")--;
      Logger::warn(str::format("D3D9DC: texture surface GetDC called tex=", (void *)parent_,
                               " lv=", level_));
    }
    static bool warned = false;
    if (!warned) { warned = true; Logger::warn("D3D9: texture surface GetDC is not implemented"); }
    return D3DERR_INVALIDCALL;
  }
  HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) final { return D3DERR_INVALIDCALL; }

  // Internal accessors for render target usage
  Rc<Texture> &texture() { return parent_->texture(); }
  TextureViewKey viewKey() const { return parent_->viewKey(); }
  WMTPixelFormat mtlFormat() const { return parent_->texture()->pixelFormat(); }
  D3D9Texture2D *parentTexture() const { return parent_; }

private:
  D3D9Texture2D *parent_;
  UINT level_;
};

// Deferred implementation — needs D3D9TextureSurface to be complete
inline HRESULT STDMETHODCALLTYPE D3D9Texture2D::GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurfaceLevel) {
  if (Level >= levelCount_ || !ppSurfaceLevel) return D3DERR_INVALIDCALL;
  if (DebugTraceBudget("MSE_TRACE_TEX") > 0) {
    DebugTraceBudget("MSE_TRACE_TEX")--;
    Logger::info(str::format("#", DebugTraceSeq(), " GetSurfaceLevel tex=", (void *)this, " lv=",
                             Level));
  }
  *ppSurfaceLevel = ref(new D3D9TextureSurface(this, Level));
  return S_OK;
}

} // namespace dxmt
