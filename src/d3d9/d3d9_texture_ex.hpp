#pragma once

#include "com/com_object.hpp"
#include "dxmt_command_queue.hpp"
#include "dxmt_texture.hpp"
#include "d3d9_format.hpp"
#include "d3d9_getdc.hpp"
#include "log/log.hpp"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace dxmt {

class D3D9Device;

/*
 * What the draw path needs from any bound texture.
 *
 * IDirect3DTexture9 (2D), IDirect3DVolumeTexture9 (3D) and IDirect3DCubeTexture9
 * all end up as one Metal texture with one default view; the device binds
 * through this interface so the three resource types share a single path.
 */
class D3D9BaseTexture9 {
public:
  virtual ~D3D9BaseTexture9() {}
  virtual Rc<Texture> &gpuTexture() = 0;
  virtual TextureViewKey defaultView() = 0;
  virtual D3DFORMAT d3dFormat() = 0;
  virtual UINT baseWidth() = 0;
  virtual UINT baseHeight() = 0;
  virtual bool anyDirty() = 0;
  virtual void markAllDirty() = 0;
  virtual void uploadDirty(dxmt::CommandQueue &queue) = 0;
  virtual size_t nonZeroBytes() = 0;
  virtual void debugStats(char *out, size_t size) = 0;
};

/* One mip level of a staged (CPU-visible) texture.  For cube textures the
 * slices are the six faces, for volume textures they are depth slices. */
struct D3D9StagedLevel {
  UINT width = 0;
  UINT height = 0;
  UINT slices = 1;
  UINT pitch = 0;
  size_t sliceSize = 0;
  size_t dataSize = 0;
  void *data = nullptr;
  bool dirty = false;
};

/*
 * Upload every dirty level of a staged texture.
 *
 * The staged data is laid out slice by slice (cube faces, volume slices), which
 * is exactly what a single copy-from-buffer-to-texture blit per slice expects.
 */
inline void D3D9UploadStagedLevels(dxmt::CommandQueue &queue, Rc<Texture> &gpu,
                                   std::vector<D3D9StagedLevel> &levels, bool is3d) {
  for (UINT level = 0; level < levels.size(); level++) {
    auto &lvl = levels[level];
    if (!lvl.dirty || !lvl.data)
      continue;

    auto staging = queue.AllocateTransientBuffer(lvl.dataSize, 16);
    if (!staging.cpu_ptr) {
      Logger::warn("D3D9: transient upload allocation failed for a staged level");
      continue;
    }
    std::memcpy(staging.cpu_ptr, lvl.data, lvl.dataSize);

    obj_handle_t stagingHandle = staging.buffer.handle;
    uint64_t stagingOffset = (uint64_t)staging.offset;
    UINT slices = lvl.slices;
    UINT width = lvl.width;
    UINT height = lvl.height;
    UINT pitch = lvl.pitch;
    uint64_t sliceSize = lvl.sliceSize;

    auto chunk = queue.CurrentChunk();
    chunk->emitcc([gpu, stagingHandle, stagingOffset, level, slices, width, height, pitch,
                   sliceSize, is3d](ArgumentEncodingContext &ctx) mutable {
      ctx.startBlitPass();
      for (UINT slice = 0; slice < slices; slice++) {
        auto dstHandle = ctx.access(gpu, 0u, 0u, DXMT_ENCODER_RESOURCE_ACESS_WRITE);
        auto &blitCmd = ctx.encodeBlitCommand<wmtcmd_blit_copy_from_buffer_to_texture>();
        blitCmd.type = WMTBlitCommandCopyFromBufferToTexture;
        blitCmd.src = stagingHandle;
        blitCmd.src_offset = stagingOffset + (uint64_t)slice * sliceSize;
        blitCmd.bytes_per_row = pitch;
        blitCmd.bytes_per_image = (uint32_t)sliceSize;
        blitCmd.size = {width, height, 1};
        blitCmd.dst = dstHandle;
        // Cube faces live in the array slices, volume depth in the z axis.
        blitCmd.slice = is3d ? 0 : slice;
        blitCmd.level = level;
        blitCmd.origin = {0, 0, is3d ? (uint32_t)slice : 0u};
      }
      ctx.endPass();
    });

    lvl.dirty = false;
  }
}

/*
 * IDirect3DVolume9 - one mip level of a volume texture.
 */
class D3D9Texture3D;

class D3D9Volume final : public ComObjectClamp<IDirect3DVolume9> {
public:
  D3D9Volume(D3D9Texture3D *parent, UINT level);
  ~D3D9Volume();

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) final;
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) final;
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **ppContainer) final;
  HRESULT STDMETHODCALLTYPE GetDesc(D3DVOLUME_DESC *pDesc) final;
  HRESULT STDMETHODCALLTYPE LockBox(D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox, DWORD Flags) final;
  HRESULT STDMETHODCALLTYPE UnlockBox() final;

  D3D9Texture3D *parent() const { return parent_; }
  UINT level() const { return level_; }

private:
  D3D9Texture3D *parent_;
  UINT level_;
  bool locked_ = false;
};

/*
 * IDirect3DVolumeTexture9 - a staged 3D texture.
 */
class D3D9Texture3D final : public ComObjectClamp<IDirect3DVolumeTexture9>,
                            public D3D9BaseTexture9 {
public:
  friend class D3D9Volume;

  D3D9Texture3D(D3D9Device *device, UINT width, UINT height, UINT depth, UINT levels,
                D3DFORMAT format, Rc<Texture> texture, TextureViewKey viewKey);
  ~D3D9Texture3D();

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) final;
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) final;
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) final { return D3DERR_INVALIDCALL; }
  DWORD STDMETHODCALLTYPE SetPriority(DWORD) final { return 0; }
  DWORD STDMETHODCALLTYPE GetPriority() final { return 0; }
  void STDMETHODCALLTYPE PreLoad() final {}
  D3DRESOURCETYPE STDMETHODCALLTYPE GetType() final { return D3DRTYPE_VOLUMETEXTURE; }

  DWORD STDMETHODCALLTYPE SetLOD(DWORD) final { return levelCount_; }
  DWORD STDMETHODCALLTYPE GetLOD() final { return levelCount_; }
  DWORD STDMETHODCALLTYPE GetLevelCount() final { return levelCount_; }
  HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) final { return S_OK; }
  D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() final { return D3DTEXF_NONE; }
  void STDMETHODCALLTYPE GenerateMipSubLevels() final {}

  HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DVOLUME_DESC *pDesc) final;
  HRESULT STDMETHODCALLTYPE GetVolumeLevel(UINT Level, IDirect3DVolume9 **ppVolumeLevel) final;
  HRESULT STDMETHODCALLTYPE LockBox(UINT Level, D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox,
                                    DWORD Flags) final;
  HRESULT STDMETHODCALLTYPE UnlockBox(UINT Level) final;
  HRESULT STDMETHODCALLTYPE AddDirtyBox(const D3DBOX *) final { return S_OK; }

  // D3D9BaseTexture9
  Rc<Texture> &gpuTexture() final { return texture_; }
  TextureViewKey defaultView() final { return view_key_; }
  D3DFORMAT d3dFormat() final { return format_; }
  UINT baseWidth() final { return width_; }
  UINT baseHeight() final { return height_; }
  bool anyDirty() final;
  void markAllDirty() final;
  void uploadDirty(dxmt::CommandQueue &queue) final;
  size_t nonZeroBytes() final;
  void debugStats(char *out, size_t size) final;

  D3D9Device *device() const { return device_; }

private:
  D3D9Device *device_;
  UINT width_, height_, depth_;
  D3DFORMAT format_;
  UINT levelCount_;
  std::vector<D3D9StagedLevel> levels_;
  Rc<Texture> texture_;
  TextureViewKey view_key_ = 0;
};

/*
 * IDirect3DSurface9 for a cube face.  Faces are the array slices of the Metal
 * cube texture, so a face is one view with firstArraySlice = face.
 */
class D3D9TextureCube;

class D3D9CubeSurface final : public ComObjectClamp<IDirect3DSurface9> {
public:
  D3D9CubeSurface(D3D9TextureCube *parent, D3DCUBEMAP_FACES face, UINT level);
  ~D3D9CubeSurface();

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) final;
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) final;
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) final { return D3DERR_INVALIDCALL; }
  DWORD STDMETHODCALLTYPE SetPriority(DWORD) final { return 0; }
  DWORD STDMETHODCALLTYPE GetPriority() final { return 0; }
  void STDMETHODCALLTYPE PreLoad() final {}
  D3DRESOURCETYPE STDMETHODCALLTYPE GetType() final { return D3DRTYPE_SURFACE; }
  HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **ppContainer) final;
  HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *pDesc) final;
  HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) final;
  HRESULT STDMETHODCALLTYPE UnlockRect() final;
  HRESULT STDMETHODCALLTYPE GetDC(HDC *phdc) final;
  HRESULT STDMETHODCALLTYPE ReleaseDC(HDC hdc) final;

  D3D9TextureCube *parent() const { return parent_; }
  D3DCUBEMAP_FACES face() const { return face_; }
  UINT level() const { return level_; }
  TextureViewKey faceView() const { return face_view_; }

private:
  D3D9TextureCube *parent_;
  D3DCUBEMAP_FACES face_;
  UINT level_;
  TextureViewKey face_view_ = 0;
  bool locked_ = false;
  D3D9SurfaceDC gdi_dc_;
};

/*
 * IDirect3DCubeTexture9.
 */
class D3D9TextureCube final : public ComObjectClamp<IDirect3DCubeTexture9>,
                              public D3D9BaseTexture9 {
public:
  friend class D3D9CubeSurface;

  D3D9TextureCube(D3D9Device *device, UINT edgeLength, UINT levels, D3DFORMAT format,
                  Rc<Texture> texture);
  ~D3D9TextureCube();

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) final;
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) final;
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) final { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) final { return D3DERR_INVALIDCALL; }
  DWORD STDMETHODCALLTYPE SetPriority(DWORD) final { return 0; }
  DWORD STDMETHODCALLTYPE GetPriority() final { return 0; }
  void STDMETHODCALLTYPE PreLoad() final {}
  D3DRESOURCETYPE STDMETHODCALLTYPE GetType() final { return D3DRTYPE_CUBETEXTURE; }

  DWORD STDMETHODCALLTYPE SetLOD(DWORD) final { return levelCount_; }
  DWORD STDMETHODCALLTYPE GetLOD() final { return levelCount_; }
  DWORD STDMETHODCALLTYPE GetLevelCount() final { return levelCount_; }
  HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) final { return S_OK; }
  D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() final { return D3DTEXF_NONE; }
  void STDMETHODCALLTYPE GenerateMipSubLevels() final {}

  HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) final;
  HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level,
                                              IDirect3DSurface9 **ppCubeMapSurface) final;
  HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES FaceType, UINT Level,
                                     D3DLOCKED_RECT *pLockedRect, const RECT *pRect,
                                     DWORD Flags) final;
  HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES FaceType, UINT Level) final;
  HRESULT STDMETHODCALLTYPE AddDirtyRect(D3DCUBEMAP_FACES FaceType, const RECT *) final { return S_OK; }

  // D3D9BaseTexture9
  Rc<Texture> &gpuTexture() final { return texture_; }
  TextureViewKey defaultView() final { return view_key_; }
  D3DFORMAT d3dFormat() final { return format_; }
  UINT baseWidth() final { return edge_; }
  UINT baseHeight() final { return edge_; }
  bool anyDirty() final;
  void markAllDirty() final;
  void uploadDirty(dxmt::CommandQueue &queue) final;
  size_t nonZeroBytes() final;
  void debugStats(char *out, size_t size) final;

  D3D9Device *device() const { return device_; }

private:
  D3D9Device *device_;
  UINT edge_;
  D3DFORMAT format_;
  UINT levelCount_;
  std::vector<D3D9StagedLevel> levels_; // slices_ = 6 (faces)
  Rc<Texture> texture_;
  TextureViewKey view_key_ = 0;
  TextureViewKey face_views_[6] = {};
};

} // namespace dxmt
