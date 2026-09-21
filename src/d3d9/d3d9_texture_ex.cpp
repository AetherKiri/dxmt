#include "d3d9_texture_ex.hpp"
#include "d3d9_device.hpp"

namespace dxmt {

// ---------------------------------------------------------------------------
// IDirect3DVolume9
// ---------------------------------------------------------------------------

D3D9Volume::D3D9Volume(D3D9Texture3D *parent, UINT level) : parent_(parent), level_(level) {
  parent_->AddRef();
}

D3D9Volume::~D3D9Volume() {
  if (parent_)
    parent_->Release();
}

HRESULT STDMETHODCALLTYPE D3D9Volume::QueryInterface(REFIID riid, void **ppvObj) {
  if (!ppvObj)
    return E_POINTER;
  *ppvObj = nullptr;
  if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DResource9) ||
      riid == __uuidof(IDirect3DVolume9)) {
    *ppvObj = ref(this);
    return S_OK;
  }
  return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE D3D9Volume::GetDevice(IDirect3DDevice9 **ppDevice) {
  if (!ppDevice)
    return D3DERR_INVALIDCALL;
  *ppDevice = ref(static_cast<IDirect3DDevice9 *>(parent_->device()));
  return S_OK;
}

HRESULT STDMETHODCALLTYPE D3D9Volume::GetContainer(REFIID riid, void **ppContainer) {
  if (!ppContainer)
    return E_POINTER;
  return parent_->QueryInterface(riid, ppContainer);
}

HRESULT STDMETHODCALLTYPE D3D9Volume::GetDesc(D3DVOLUME_DESC *pDesc) {
  return parent_->GetLevelDesc(level_, pDesc);
}

HRESULT STDMETHODCALLTYPE D3D9Volume::LockBox(D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox,
                                             DWORD Flags) {
  if (getenv("MSE_D3D9_CONF_TRACE")) {
    fprintf(stderr, "D3D9CONF VolumeLockBox a=%p level=%u\n", (void *)pLockedVolume, level_);
    fflush(stderr);
  }
  return parent_->LockBox(level_, pLockedVolume, pBox, Flags);
}

HRESULT STDMETHODCALLTYPE D3D9Volume::UnlockBox() {
  return parent_->UnlockBox(level_);
}

// ---------------------------------------------------------------------------
// IDirect3DVolumeTexture9
// ---------------------------------------------------------------------------

D3D9Texture3D::D3D9Texture3D(D3D9Device *device, UINT width, UINT height, UINT depth,
                             UINT levels, D3DFORMAT format, Rc<Texture> texture,
                             TextureViewKey viewKey)
    : device_(device), width_(width), height_(height), depth_(depth), format_(format),
      texture_(std::move(texture)), view_key_(viewKey) {
  if (levels == 0)
    levels = (UINT)std::floor(std::log2((double)std::max(std::max(width, height), depth))) + 1;
  levelCount_ = levels;

  UINT w = width, h = height, d = depth;
  for (UINT i = 0; i < levelCount_; i++) {
    D3D9StagedLevel level;
    level.width = w;
    level.height = h;
    level.slices = d;
    level.pitch = D3D9FormatPitch(format, w);
    level.sliceSize = (size_t)level.pitch * h;
    level.dataSize = level.sliceSize * d;
    level.data = std::malloc(level.dataSize);
    std::memset(level.data, 0, level.dataSize);
    levels_.push_back(level);
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
    d = std::max(1u, d / 2);
  }
}

D3D9Texture3D::~D3D9Texture3D() {
  for (auto &level : levels_)
    std::free(level.data);
}

HRESULT STDMETHODCALLTYPE D3D9Texture3D::QueryInterface(REFIID riid, void **ppvObj) {
  if (!ppvObj)
    return E_POINTER;
  *ppvObj = nullptr;
  if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DResource9) ||
      riid == __uuidof(IDirect3DBaseTexture9) || riid == __uuidof(IDirect3DVolumeTexture9)) {
    *ppvObj = ref(this);
    return S_OK;
  }
  return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE D3D9Texture3D::GetDevice(IDirect3DDevice9 **ppDevice) {
  if (!ppDevice)
    return D3DERR_INVALIDCALL;
  *ppDevice = ref(static_cast<IDirect3DDevice9 *>(device_));
  return S_OK;
}

HRESULT STDMETHODCALLTYPE D3D9Texture3D::GetLevelDesc(UINT Level, D3DVOLUME_DESC *pDesc) {
  if (Level >= levelCount_ || !pDesc)
    return D3DERR_INVALIDCALL;
  const auto &level = levels_[Level];
  pDesc->Format = format_;
  pDesc->Type = D3DRTYPE_VOLUME;
  pDesc->Usage = 0;
  pDesc->Pool = D3DPOOL_MANAGED;
  pDesc->Width = level.width;
  pDesc->Height = level.height;
  pDesc->Depth = level.slices;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE D3D9Texture3D::GetVolumeLevel(UINT Level,
                                                        IDirect3DVolume9 **ppVolumeLevel) {
  if (Level >= levelCount_ || !ppVolumeLevel)
    return D3DERR_INVALIDCALL;
  *ppVolumeLevel = ref(new D3D9Volume(this, Level));
  return S_OK;
}

HRESULT STDMETHODCALLTYPE D3D9Texture3D::LockBox(UINT Level, D3DLOCKED_BOX *pLockedVolume,
                                                const D3DBOX *pBox, DWORD Flags) {
  if (Level >= levelCount_ || !pLockedVolume)
    return D3DERR_INVALIDCALL;
  auto &level = levels_[Level];
  UINT bytesPerPixel = D3D9FormatBytesPerPixel(format_);

  pLockedVolume->RowPitch = level.pitch;
  pLockedVolume->SlicePitch = (UINT)level.sliceSize;
  if (pBox) {
    pLockedVolume->pBits = (uint8_t *)level.data + (size_t)pBox->Front * level.sliceSize +
                           (size_t)pBox->Top * level.pitch + (size_t)pBox->Left * bytesPerPixel;
  } else {
    pLockedVolume->pBits = level.data;
  }
  if (!(Flags & D3DLOCK_READONLY))
    level.dirty = true;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE D3D9Texture3D::UnlockBox(UINT Level) {
  if (Level >= levelCount_)
    return D3DERR_INVALIDCALL;
  levels_[Level].dirty = true;
  return S_OK;
}

bool D3D9Texture3D::anyDirty() {
  for (auto &level : levels_) {
    if (level.dirty)
      return true;
  }
  return false;
}

void D3D9Texture3D::markAllDirty() {
  for (auto &level : levels_)
    level.dirty = true;
}

void D3D9Texture3D::uploadDirty(dxmt::CommandQueue &queue) {
  D3D9UploadStagedLevels(queue, texture_, levels_, true);
}

size_t D3D9Texture3D::nonZeroBytes() {
  size_t total = 0;
  for (auto &level : levels_) {
    const uint8_t *bytes = (const uint8_t *)level.data;
    for (size_t i = 0; i < level.dataSize; i++) {
      if (bytes[i])
        total++;
    }
  }
  return total;
}

void D3D9Texture3D::debugStats(char *out, size_t size) {
  snprintf(out, size, "vol %ux%ux%u lv=%u fmt=%d nz=%zu", width_, height_, depth_,
           levelCount_, (int)format_, nonZeroBytes());
}

// ---------------------------------------------------------------------------
// IDirect3DSurface9 for a cube face
// ---------------------------------------------------------------------------

D3D9CubeSurface::D3D9CubeSurface(D3D9TextureCube *parent, D3DCUBEMAP_FACES face, UINT level)
    : parent_(parent), face_(face), level_(level) {
  parent_->AddRef();

  D3DSURFACE_DESC desc = {};
  parent_->GetLevelDesc(level_, &desc);
  TextureViewDescriptor viewDesc = {
      .format = parent_->gpuTexture()->pixelFormat(),
      .type = WMTTextureType2D,
      .firstMiplevel = level,
      .miplevelCount = 1,
      .firstArraySlice = (uint32_t)face,
      .arraySize = 1,
  };
  face_view_ = parent_->gpuTexture()->createView(viewDesc);
}

D3D9CubeSurface::~D3D9CubeSurface() {
  if (parent_)
    parent_->Release();
}

HRESULT STDMETHODCALLTYPE D3D9CubeSurface::QueryInterface(REFIID riid, void **ppvObj) {
  if (!ppvObj)
    return E_POINTER;
  *ppvObj = nullptr;
  if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DResource9) ||
      riid == __uuidof(IDirect3DSurface9)) {
    *ppvObj = ref(this);
    return S_OK;
  }
  return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE D3D9CubeSurface::GetDevice(IDirect3DDevice9 **ppDevice) {
  if (!ppDevice)
    return D3DERR_INVALIDCALL;
  *ppDevice = ref(static_cast<IDirect3DDevice9 *>(parent_->device()));
  return S_OK;
}

HRESULT STDMETHODCALLTYPE D3D9CubeSurface::GetContainer(REFIID riid, void **ppContainer) {
  if (!ppContainer)
    return E_POINTER;
  return parent_->QueryInterface(riid, ppContainer);
}

HRESULT STDMETHODCALLTYPE D3D9CubeSurface::GetDesc(D3DSURFACE_DESC *pDesc) {
  return parent_->GetLevelDesc(level_, pDesc);
}

HRESULT STDMETHODCALLTYPE D3D9CubeSurface::LockRect(D3DLOCKED_RECT *pLockedRect,
                                                   const RECT *pRect, DWORD Flags) {
  if (locked_)
    return D3DERR_INVALIDCALL;
  HRESULT hr = parent_->LockRect(face_, level_, pLockedRect, pRect, Flags);
  if (SUCCEEDED(hr))
    locked_ = true;
  return hr;
}

HRESULT STDMETHODCALLTYPE D3D9CubeSurface::UnlockRect() {
  if (!locked_)
    return D3DERR_INVALIDCALL;
  locked_ = false;
  return parent_->UnlockRect(face_, level_);
}

HRESULT STDMETHODCALLTYPE D3D9CubeSurface::GetDC(HDC *phdc) {
  D3DSURFACE_DESC desc = {};
  if (FAILED(GetDesc(&desc)))
    return D3DERR_INVALIDCALL;
  return gdi_dc_.acquire(phdc, this, desc.Width, desc.Height, desc.Format, false);
}

HRESULT STDMETHODCALLTYPE D3D9CubeSurface::ReleaseDC(HDC hdc) {
  return gdi_dc_.release(hdc);
}

// ---------------------------------------------------------------------------
// IDirect3DCubeTexture9
// ---------------------------------------------------------------------------

D3D9TextureCube::D3D9TextureCube(D3D9Device *device, UINT edgeLength, UINT levels,
                                 D3DFORMAT format, Rc<Texture> texture)
    : device_(device), edge_(edgeLength), format_(format), texture_(std::move(texture)) {
  if (levels == 0)
    levels = (UINT)std::floor(std::log2((double)edgeLength)) + 1;
  levelCount_ = levels;

  TextureViewDescriptor viewDesc = {
      .format = texture_->pixelFormat(),
      .type = WMTTextureTypeCube,
      .firstMiplevel = 0,
      .miplevelCount = levelCount_,
      .firstArraySlice = 0,
      .arraySize = 6,
  };
  view_key_ = texture_->createView(viewDesc);

  UINT size = edgeLength;
  for (UINT i = 0; i < levelCount_; i++) {
    D3D9StagedLevel level;
    level.width = size;
    level.height = size;
    level.slices = 6;
    level.pitch = D3D9FormatPitch(format, size);
    level.sliceSize = (size_t)level.pitch * size;
    level.dataSize = level.sliceSize * 6;
    level.data = std::malloc(level.dataSize);
    std::memset(level.data, 0, level.dataSize);
    levels_.push_back(level);
    size = std::max(1u, size / 2);
  }
}

D3D9TextureCube::~D3D9TextureCube() {
  for (auto &level : levels_)
    std::free(level.data);
}

HRESULT STDMETHODCALLTYPE D3D9TextureCube::QueryInterface(REFIID riid, void **ppvObj) {
  if (!ppvObj)
    return E_POINTER;
  *ppvObj = nullptr;
  if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DResource9) ||
      riid == __uuidof(IDirect3DBaseTexture9) || riid == __uuidof(IDirect3DCubeTexture9)) {
    *ppvObj = ref(this);
    return S_OK;
  }
  return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE D3D9TextureCube::GetDevice(IDirect3DDevice9 **ppDevice) {
  if (!ppDevice)
    return D3DERR_INVALIDCALL;
  *ppDevice = ref(static_cast<IDirect3DDevice9 *>(device_));
  return S_OK;
}

HRESULT STDMETHODCALLTYPE D3D9TextureCube::GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) {
  if (Level >= levelCount_ || !pDesc)
    return D3DERR_INVALIDCALL;
  pDesc->Format = format_;
  pDesc->Type = D3DRTYPE_SURFACE;
  pDesc->Usage = 0;
  pDesc->Pool = D3DPOOL_MANAGED;
  pDesc->MultiSampleType = D3DMULTISAMPLE_NONE;
  pDesc->MultiSampleQuality = 0;
  pDesc->Width = levels_[Level].width;
  pDesc->Height = levels_[Level].height;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE D3D9TextureCube::GetCubeMapSurface(D3DCUBEMAP_FACES FaceType,
                                                            UINT Level,
                                                            IDirect3DSurface9 **ppCubeMapSurface) {
  if (FaceType > D3DCUBEMAP_FACE_NEGATIVE_Z || Level >= levelCount_ || !ppCubeMapSurface)
    return D3DERR_INVALIDCALL;
  *ppCubeMapSurface = ref(new D3D9CubeSurface(this, FaceType, Level));
  return S_OK;
}

HRESULT STDMETHODCALLTYPE D3D9TextureCube::LockRect(D3DCUBEMAP_FACES FaceType, UINT Level,
                                                   D3DLOCKED_RECT *pLockedRect,
                                                   const RECT *pRect, DWORD Flags) {
  if (FaceType > D3DCUBEMAP_FACE_NEGATIVE_Z || Level >= levelCount_ || !pLockedRect)
    return D3DERR_INVALIDCALL;
  auto &level = levels_[Level];
  UINT bytesPerPixel = D3D9FormatBytesPerPixel(format_);
  uint8_t *face = (uint8_t *)level.data + (size_t)FaceType * level.sliceSize;

  pLockedRect->Pitch = level.pitch;
  if (pRect) {
    pLockedRect->pBits = face + (size_t)pRect->top * level.pitch +
                         (size_t)pRect->left * bytesPerPixel;
  } else {
    pLockedRect->pBits = face;
  }
  if (!(Flags & D3DLOCK_READONLY))
    level.dirty = true;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE D3D9TextureCube::UnlockRect(D3DCUBEMAP_FACES FaceType, UINT Level) {
  if (FaceType > D3DCUBEMAP_FACE_NEGATIVE_Z || Level >= levelCount_)
    return D3DERR_INVALIDCALL;
  levels_[Level].dirty = true;
  return S_OK;
}

bool D3D9TextureCube::anyDirty() {
  for (auto &level : levels_) {
    if (level.dirty)
      return true;
  }
  return false;
}

void D3D9TextureCube::markAllDirty() {
  for (auto &level : levels_)
    level.dirty = true;
}

void D3D9TextureCube::uploadDirty(dxmt::CommandQueue &queue) {
  D3D9UploadStagedLevels(queue, texture_, levels_, false);
}

size_t D3D9TextureCube::nonZeroBytes() {
  size_t total = 0;
  for (auto &level : levels_) {
    const uint8_t *bytes = (const uint8_t *)level.data;
    for (size_t i = 0; i < level.dataSize; i++) {
      if (bytes[i])
        total++;
    }
  }
  return total;
}

void D3D9TextureCube::debugStats(char *out, size_t size) {
  snprintf(out, size, "cube %u lv=%u fmt=%d nz=%zu", edge_, levelCount_, (int)format_,
           nonZeroBytes());
}

} // namespace dxmt
