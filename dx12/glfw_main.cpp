// Copyright (c) 2020 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#define NOMINMAX                     1
#define WIN32_LEAN_AND_MEAN          1
#define GLM_FORCE_RADIANS            1
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  1
#define TINYOBJLOADER_IMPLEMENTATION 1
#define STB_IMAGE_IMPLEMENTATION     1
#define GLFW_EXPOSE_NATIVE_WIN32     1

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <Windows.h>
#include <atlbase.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <stb_image.h>
#include <tiny_obj_loader.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_glfw.h"

#if RNDRX_ENABLE_DX12_DEBUG_LAYER
#  include <dxgidebug.h>
#endif

struct noncopyable {
  noncopyable() = default;
  noncopyable(noncopyable const&) = delete;
  noncopyable& operator=(noncopyable const&) = delete;
  noncopyable(noncopyable&&) = default;
  noncopyable& operator=(noncopyable&&) = default;
};

enum class LogLevel {
  None,
  Error,
  Warn,
  Info,
  Trace,
  NumLogLevels,
};

LogLevel const g_LogLevel = LogLevel::Trace;
struct LogState : noncopyable {
  LogState(std::ostream& os)
      : os_(os) {
  }

  bool is_done() const {
    return done_;
  }

  void done() {
    done_ = true;
    os_ << std::endl;
  }

  std::ostream& os_;
  bool done_ = false;
};

#define LOG(level)                                                             \
  for(LogState log_state(std::cerr);                                           \
      g_LogLevel >= LogLevel::level && !log_state.is_done();                   \
      log_state.done())                                                        \
  std::cerr

void throw_error(HRESULT r) {
  if(r != S_OK) {
    int err = ::GetLastError();
    std::stringstream msg;
    msg << "Error: hr=" << r << ", GetLastError=" << err;
    throw std::runtime_error(msg.str());
  }
}

// Convert a wide Unicode string to an UTF8 string
std::string utf16_to_utf8(std::wstring const& str) {
  std::string ret;
  if(str.empty()) {
    return ret;
  }

  int encoded_size = WideCharToMultiByte(
      CP_UTF8,
      0,
      str.data(),
      static_cast<int>(str.size()),
      nullptr,
      0,
      nullptr,
      nullptr);

  ret.resize(encoded_size);

  WideCharToMultiByte(
      CP_UTF8,
      0,
      str.data(),
      static_cast<int>(str.size()),
      ret.data(),
      encoded_size,
      nullptr,
      nullptr);

  return ret;
}

class Window : noncopyable {
 public:
  Window() {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(width_, height_, "rndrx-dx12", NULL, NULL);
    hwnd_ = glfwGetWin32Window(window_);
  }

  ~Window() {
    glfwDestroyWindow(window_);
  }

  GLFWwindow* glfw() const {
    return window_;
  }

  HWND hwnd() const {
    return hwnd_;
  }

  int width() const {
    return width_;
  }

  int height() const {
    return height_;
  }

  enum class SizeEvent {
    None,
    Changed,
  };

  SizeEvent handle_window_size() {
    int old_width = width_;
    int old_height = height_;
    glfwGetFramebufferSize(window_, &width_, &height_);
    if(width_ != old_width || height_ != old_height) {
      return SizeEvent::Changed;
    }

    return SizeEvent::None;
  }

 private:
  GLFWwindow* window_ = nullptr;
  HWND hwnd_ = nullptr;
  int width_ = 1920;
  int height_ = 1080;
};

class DescriptorPool;
class DescriptorHandle : noncopyable {
 public:
  DescriptorHandle() = default;
  DescriptorHandle(DescriptorHandle&& other)
      : owner_(other.owner_)
      , cpu_(other.cpu_)
      , gpu_(other.gpu_) {
    other.owner_ = nullptr;
  }

  DescriptorHandle& operator=(DescriptorHandle&& other) {
    owner_ = other.owner_;
    cpu_ = other.cpu_;
    gpu_ = other.gpu_;
    other.owner_ = nullptr;
    return *this;
  }

  ~DescriptorHandle();

  D3D12_CPU_DESCRIPTOR_HANDLE const& cpu() const {
    return cpu_;
  }

  D3D12_GPU_DESCRIPTOR_HANDLE const& gpu() const {
    return gpu_;
  }

  void release() {
    owner_ = nullptr;
  }

 private:
  friend class DescriptorPool;
  DescriptorHandle(
      DescriptorPool& owner,
      D3D12_CPU_DESCRIPTOR_HANDLE cpu,
      D3D12_GPU_DESCRIPTOR_HANDLE gpu)
      : owner_(&owner)
      , cpu_(cpu)
      , gpu_(gpu) {
  }
  DescriptorPool* owner_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu_ = {};
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_ = {};
};

class DescriptorPool : noncopyable {
 public:
  DescriptorPool(
      ID3D12Device* device,
      D3D12_DESCRIPTOR_HEAP_TYPE type,
      D3D12_DESCRIPTOR_HEAP_FLAGS flags,
      UINT count) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = type;
    desc.NumDescriptors = count;
    desc.Flags = flags;
    throw_error(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_)));
    cache_descriptor_handles(device, desc);
  }

  DescriptorHandle allocate() {
    DescriptorHandle ret(*this, free_cpu_handles_.back(), free_gpu_handles_.back());
    free_cpu_handles_.pop_back();
    free_gpu_handles_.pop_back();
    return ret;
  }

  template <typename OutputIterator>
  void allocate(std::size_t count, OutputIterator iter) {
    for(std::size_t i = 0; i < count; ++i) {
      *iter++ = allocate();
    }
  }

  void free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
    free_cpu_handles_.push_back(cpu);
    free_gpu_handles_.push_back(gpu);
  }

  ID3D12DescriptorHeap* heap() const {
    return heap_;
  }

 private:
  void cache_descriptor_handles(
      ID3D12Device* device,
      D3D12_DESCRIPTOR_HEAP_DESC const& desc) {
    auto size = device->GetDescriptorHandleIncrementSize(desc.Type);
    auto cpu_handle = heap_->GetCPUDescriptorHandleForHeapStart();
    std::generate_n(
        std::back_inserter(free_cpu_handles_),
        desc.NumDescriptors,
        [&cpu_handle, size] {
          auto ret = cpu_handle;
          cpu_handle.ptr += size;
          return ret;
        });

    auto gpu_handle = heap_->GetGPUDescriptorHandleForHeapStart();
    std::generate_n(
        std::back_inserter(free_gpu_handles_),
        desc.NumDescriptors,
        [&gpu_handle, size] {
          auto ret = gpu_handle;
          gpu_handle.ptr += size;
          return ret;
        });
  }

  CComPtr<ID3D12DescriptorHeap> heap_;
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> free_cpu_handles_;
  std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> free_gpu_handles_;
};

DescriptorHandle::~DescriptorHandle() {
  if(owner_) {
    owner_->free(cpu_, gpu_);
  }
}

class Device : noncopyable {
 public:
  Device(IDXGIAdapter* adapter)
      : device_(adapter)
      , adapter_(adapter)
      , rtv_pool_(
            device_.ptr,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            128)
      , srv_pool_(
            device_.ptr,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
            128)
      , dsv_pool_(
            device_.ptr,
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            128) {
    create_graphics_queue();
    cache_heap_properties();
  }

  ~Device() {
  }

  Device(Device const&) = delete;
  Device& operator=(Device const&) = delete;

  ID3D12Device4* get() const {
    return device_.ptr;
  }

  ID3D12CommandQueue* graphics_queue() const {
    return graphics_queue_;
  }

  IDXGIAdapter* adapter() const {
    return adapter_;
  }

  DescriptorPool& rtv_pool() {
    return rtv_pool_;
  }

  DescriptorPool& srv_pool() {
    return srv_pool_;
  }

  DescriptorPool& dsv_pool() {
    return dsv_pool_;
  }

  D3D12_HEAP_PROPERTIES const& resource_heap() const {
    return resource_heap_;
  }

  D3D12_HEAP_PROPERTIES const& upload_heap() const {
    return upload_heap_;
  }

 private:
  struct DeviceCreator {
    DeviceCreator(IDXGIAdapter* adapter) {
#if RNDRX_ENABLE_DX12_DEBUG_LAYER
      CComPtr<ID3D12Debug> dx12_debug;
      if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dx12_debug)))) {
        dx12_debug->EnableDebugLayer();
      }
#endif

      D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
      throw_error(D3D12CreateDevice(adapter, featureLevel, IID_PPV_ARGS(&ptr)));

#if RNDRX_ENABLE_DX12_DEBUG_LAYER
      if(dx12_debug) {
        CComPtr<ID3D12InfoQueue> info_queue;
        ptr->QueryInterface(&info_queue);
        info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
      }
#endif
    }

    CComPtr<ID3D12Device4> ptr;
  };

  void create_graphics_queue() {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    auto* device = get();
    throw_error(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&graphics_queue_)));
  }

  void cache_heap_properties() {
    resource_heap_ = device_.ptr->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_DEFAULT);
    upload_heap_ = device_.ptr->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_UPLOAD);
  }

  DeviceCreator device_;
  CComPtr<ID3D12CommandQueue> graphics_queue_;
  IDXGIAdapter* adapter_ = nullptr;
  DescriptorPool rtv_pool_;
  DescriptorPool srv_pool_;
  DescriptorPool dsv_pool_;
  D3D12_HEAP_PROPERTIES resource_heap_;
  D3D12_HEAP_PROPERTIES upload_heap_;
};

class SubmissionContext : noncopyable {
 public:
  SubmissionContext(Device& device)
      : device_(device) {
    throw_error(device_.get()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&command_allocator_)));
  }

  void present_fence_value(UINT64 value) {
    present_fence_value_ = value;
  }

  UINT64 present_fence_value() const {
    return present_fence_value_;
  }

  void begin_frame() {
    throw_error(command_allocator_->Reset());
  }

  void begin_rendering(ID3D12GraphicsCommandList* command_list) {
    command_list_ = command_list;
    command_list_->Reset(command_allocator_, nullptr);
    std::array<ID3D12DescriptorHeap*, 1> heaps = {device_.srv_pool().heap()};
    command_list_->SetDescriptorHeaps(heaps.size(), heaps.data());
  }

  void finish_rendering() {
    throw_error(command_list_->Close());
    std::array<ID3D12CommandList*, 1> commands = {command_list_};
    device_.graphics_queue()->ExecuteCommandLists(commands.size(), commands.data());
    command_list_ = nullptr;
  }

  ID3D12GraphicsCommandList* command_list() const {
    return command_list_;
  }

 private:
  Device& device_;
  CComPtr<ID3D12CommandAllocator> command_allocator_;
  UINT64 present_fence_value_ = 0;
  ID3D12GraphicsCommandList* command_list_;
};

class ResourceCreator : noncopyable {
 public:
  ResourceCreator(Device& device)
      : device_(device) {
    create_command_list();
    create_copy_queue();
    create_fence();
  }

  ~ResourceCreator() {
    if(copy_fence_event_) {
      CloseHandle(copy_fence_event_);
    }
  }

  CComPtr<ID3D12Resource> create_image_resource(int width, int height) {
    D3D12_RESOURCE_DESC desc = {};
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Width = width;
    desc.Height = height;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    desc.DepthOrArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    CComPtr<ID3D12Resource> image;
    throw_error(device_.get()->CreateCommittedResource(
        &device_.resource_heap(),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&image)));

    return image;
  }

  CComPtr<ID3D12Resource> create_vertex_buffer_resource(
      std::size_t vertex_count,
      std::size_t vertex_size) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = vertex_count * vertex_size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    CComPtr<ID3D12Resource> vertex_buffer;
    throw_error(device_.get()->CreateCommittedResource(
        &device_.resource_heap(),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&vertex_buffer)));

    return vertex_buffer;
  }

  void reset() {
    throw_error(command_allocator_->Reset());
  }

  void begin_loading() {
    command_list_->Reset(command_allocator_, nullptr);
  }

  void finish_loading() {
    throw_error(command_list_->Close());
    std::array<ID3D12CommandList*, 1> commands = {command_list_};
    copy_queue_->ExecuteCommandLists(commands.size(), commands.data());
    std::uint64_t signal_value = ++current_fence_value_;
    throw_error(copy_queue_->Signal(copy_fence_, signal_value));
  }

  ID3D12CommandQueue* copy_queue() const {
    return copy_queue_;
  }

  ID3D12GraphicsCommandList* command_list() {
    return command_list_;
  }

  Device& device() const {
    return device_;
  }

  void finalise_ready(SubmissionContext& sc) {
    auto flush_value = copy_fence_->GetCompletedValue();
    while(!finalisation_queue_.empty() &&
          finalisation_queue_.front().fence_value <= flush_value) {
      finalisation_queue_.front().fn(*this, sc);
      finalisation_queue_.pop();
    }
  }

  void finalise_all(SubmissionContext& sc) {
    auto completed_value = copy_fence_->GetCompletedValue();
    if(completed_value < current_fence_value_) {
      throw_error(copy_fence_->SetEventOnCompletion(
          current_fence_value_,
          copy_fence_event_));
      WaitForSingleObject(copy_fence_event_, INFINITE);
    }

    while(!finalisation_queue_.empty() &&
          finalisation_queue_.front().fence_value <= completed_value) {
      finalisation_queue_.front().fn(*this, sc);
      finalisation_queue_.pop();
    }
  }

  template <typename Fn>
  void on_finalise(Fn&& f) {
    finalisation_queue_.emplace(f, current_fence_value_);
  }

  CComPtr<ID3D12Resource> create_staging_resource(ID3D12Resource* destination) const {
    auto required_upload_size = calculate_staging_size_for_resource(destination, 0, 1);
    auto source_desc = destination->GetDesc();
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = required_upload_size;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.Alignment = 0;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    CComPtr<ID3D12Device> device = nullptr;
    destination->GetDevice(IID_PPV_ARGS(&device));
    CComPtr<ID3D12Resource> staging_resource;
    throw_error(device->CreateCommittedResource(
        &device_.upload_heap(),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&staging_resource)));
    return staging_resource;
  }

  // calculate_staging_size_for_resource, memcpy_subresource, and
  // update_subresources taken from d3dx12.h from dx12 samples.
  std::uint64_t calculate_staging_size_for_resource(
      ID3D12Resource* resource,
      unsigned index,
      unsigned count) const {
    auto desc = resource->GetDesc();
    std::uint64_t size = 0;
    CComPtr<ID3D12Device> device = nullptr;
    resource->GetDevice(IID_PPV_ARGS(&device));
    device->GetCopyableFootprints(&desc, 0, count, 0, nullptr, nullptr, nullptr, &size);
    return size;
  }

  inline void memcpy_subresource(
      D3D12_MEMCPY_DEST const* dest,
      D3D12_SUBRESOURCE_DATA const* src,
      std::size_t row_size_bytes,
      UINT row_count,
      UINT slice_count) noexcept {
    for(UINT z = 0; z < slice_count; ++z) {
      auto dest_slice = static_cast<BYTE*>(dest->pData) + dest->SlicePitch * z;
      auto src_slice = static_cast<const BYTE*>(src->pData) +
                       src->SlicePitch * LONG_PTR(z);
      for(UINT y = 0; y < row_count; ++y) {
        memcpy(
            dest_slice + dest->RowPitch * y,
            src_slice + src->RowPitch * LONG_PTR(y),
            row_size_bytes);
      }
    }
  }

  std::uint64_t update_subresources(
      ID3D12Resource* destination,
      ID3D12Resource* staging,
      UINT index,
      UINT count,
      UINT64 size,
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT const* layouts,
      UINT const* row_count,
      UINT64 const* row_size_bytes,
      D3D12_SUBRESOURCE_DATA const* source) noexcept {
    auto destination_desc = destination->GetDesc();
    std::byte* staging_mem = nullptr;

    D3D12_RANGE read_range;
    read_range.Begin = 0;
    read_range.End = 0;
    throw_error(
        staging->Map(0, &read_range, reinterpret_cast<void**>(&staging_mem)));
    for(int i = 0; i < count; ++i) {
      D3D12_MEMCPY_DEST dest_data = {
          staging_mem + layouts[i].Offset,
          layouts[i].Footprint.RowPitch,
          std::size_t(layouts[i].Footprint.RowPitch) * std::size_t(row_count[i])};
      memcpy_subresource(
          &dest_data,
          &source[i],
          static_cast<std::size_t>(row_size_bytes[i]),
          row_count[i],
          layouts[i].Footprint.Depth);
    }
    staging->Unmap(0, &read_range);

    if(destination_desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      command_list_->CopyBufferRegion(
          destination,
          0,
          staging,
          layouts[0].Offset,
          layouts[0].Footprint.Width);
    }
    else {
      for(int i = 0; i < count; ++i) {
        D3D12_TEXTURE_COPY_LOCATION dest = {};
        dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dest.pResource = destination;
        dest.SubresourceIndex = i + index;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.pResource = staging;
        src.PlacedFootprint = layouts[i];
        command_list_->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);
      }
    }
    return size;
  }

  std::uint64_t update_subresources(
      ID3D12Resource* destination,
      ID3D12Resource* staging,
      UINT64 staging_offset,
      UINT index,
      UINT count,
      D3D12_SUBRESOURCE_DATA const* source) {
    std::size_t size = 0;
    auto element_size = sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) +
                        sizeof(unsigned) + sizeof(std::size_t);
    std::vector<std::byte> buffer;
    buffer.resize(element_size * count);
    void* mem = buffer.data();
    auto layouts = static_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(mem);
    auto row_size_bytes = reinterpret_cast<std::uint64_t*>(layouts + count);
    auto row_count = reinterpret_cast<unsigned*>(row_size_bytes + count);

    auto desc = destination->GetDesc();
    CComPtr<ID3D12Device> device = nullptr;
    destination->GetDevice(IID_PPV_ARGS(&device));
    device->GetCopyableFootprints(
        &desc,
        index,
        count,
        staging_offset,
        layouts,
        row_count,
        row_size_bytes,
        &size);

    std::uint64_t result = update_subresources(
        destination,
        staging,
        index,
        count,
        size,
        layouts,
        row_count,
        row_size_bytes,
        source);

    return result;
  }

 private:
  void create_command_list() {
    auto* device = device_.get();
    throw_error(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_COPY,
        IID_PPV_ARGS(&command_allocator_)));

    throw_error(device->CreateCommandList1(
        0,
        D3D12_COMMAND_LIST_TYPE_COPY,
        D3D12_COMMAND_LIST_FLAG_NONE,
        IID_PPV_ARGS(&command_list_)));
  }

  void create_copy_queue() {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    auto* device = device_.get();
    throw_error(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&copy_queue_)));
  }

  void create_fence() {
    auto* device = device_.get();
    throw_error(
        device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copy_fence_)));
    current_fence_value_ = 1;
    copy_fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if(!copy_fence_event_) {
      throw_error(HRESULT_FROM_WIN32(GetLastError()));
    }
  }

  Device& device_;
  CComPtr<ID3D12CommandAllocator> command_allocator_;
  CComPtr<ID3D12GraphicsCommandList> command_list_;
  CComPtr<ID3D12CommandQueue> copy_queue_;
  CComPtr<ID3D12Fence> copy_fence_;
  HANDLE copy_fence_event_ = nullptr;
  std::uint64_t current_fence_value_ = 0;
  struct FinalisationNode {
    template <typename Fn>
    FinalisationNode(Fn&& fn, std::uint64_t mark)
        : fn(std::forward<Fn>(fn))
        , fence_value(mark) {
    }
    std::function<void(ResourceCreator&, SubmissionContext&)> fn;
    std::uint64_t fence_value;
  };

  std::queue<FinalisationNode> finalisation_queue_;
};

template <typename>
class ShaderCache;
class FragmentShaderHandle : noncopyable {
 public:
  ID3DBlob* code() const {
    return code_;
  }

 private:
  friend class ShaderCache<FragmentShaderHandle>;
  FragmentShaderHandle(ID3DBlob* shader)
      : code_(shader) {
  }
  ID3DBlob* code_;
};

class VertexShaderHandle : noncopyable {
 public:
  ID3DBlob* code() const {
    return code_;
  }

 private:
  friend class ShaderCache<VertexShaderHandle>;
  VertexShaderHandle(ID3DBlob* shader)
      : code_(shader) {
  }
  ID3DBlob* code_;
};

template <typename ShaderHandle>
class ShaderCache {
 public:
  ShaderCache(std::string shader_model)
      : shader_model_(std::move(shader_model)) {
  }
  void add(std::string file, std::string entry) {
#if RNDRX_ENABLE_SHADER_DEBUGGING
    // Enable better shader debugging with the graphics debugging tools.
    unsigned compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    unsigned compile_flags = 0;
#endif

    std::string source;
    std::string path = "assets/shaders/";
    path += file + ".hlsl";
    std::ifstream fin(path, std::ios::binary);
    fin.seekg(0, std::ios::end);
    auto len = fin.tellg();
    fin.seekg(0, std::ios::beg);
    source.resize(len);
    fin.read(source.data(), len);
    CComPtr<ID3DBlob> resource;
    throw_error(D3DCompile(
        source.data(),
        source.size(),
        nullptr,
        nullptr,
        nullptr,
        entry.c_str(),
        shader_model_.c_str(),
        compile_flags,
        0,
        &resource,
        nullptr));

    shaders_.emplace(
        ShaderDef(std::move(file), std::move(entry)),
        std::move(resource));
  }

  ShaderHandle find(std::string file, std::string entry) {
    auto iter = shaders_.find(ShaderDef(std::move(file), std::move(entry)));
    if(iter == shaders_.end()) {
      return nullptr;
    }
    return iter->second.p;
  }

 private:
  struct ShaderDef {
    ShaderDef(std::string f, std::string e)
        : file(std::move(f))
        , entry(std::move(e)) {
    }

    std::string file;
    std::string entry;
    friend bool operator==(ShaderDef const& a, ShaderDef const& b) {
      return a.file == b.file && a.entry == b.entry;
    }
  };

  struct HashShaderDef {
    std::size_t operator()(ShaderDef const& sd) const {
      auto hasher = std::hash<std::string>();
      return hasher(sd.file) ^ hasher(sd.entry);
    }
  };

  std::unordered_map<ShaderDef, CComPtr<ID3DBlob>, HashShaderDef> shaders_;
  std::string shader_model_;
};

class ShaderData {
 public:
  ShaderData(Device& d, std::size_t size) {
    create_constant_buffer(d, size);
    create_view(d, size);

    D3D12_RANGE read_range = {0, 0};
    throw_error(
        constant_buffer_->Map(0, &read_range, reinterpret_cast<void**>(&ptr_)));
  }

  DescriptorHandle const& view() const {
    return view_;
  }

  void write(void* source, std::size_t size) {
    std::memcpy(ptr_, source, size);
  }

 private:
  void create_constant_buffer(Device& d, std::size_t size) {
    auto* device = d.get();
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    throw_error(device->CreateCommittedResource(
        &d.upload_heap(),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&constant_buffer_)));
  }

  void create_view(Device& d, std::size_t size) {
    auto* device = d.get();
    D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
    desc.BufferLocation = constant_buffer_->GetGPUVirtualAddress();
    desc.SizeInBytes = size;
    view_ = d.srv_pool().allocate();
    device->CreateConstantBufferView(&desc, view_.cpu());
  }

  CComPtr<ID3D12Resource> constant_buffer_;
  DescriptorHandle view_;
  void* ptr_;
};

class Image : noncopyable {
 public:
  Image() = default;
  Image(CComPtr<ID3D12Resource> image, DescriptorHandle&& view)
      : image_(std::move(image))
      , view_(std::move(view)) {
  }

  Image(Device& d, CComPtr<ID3D12Resource> image)
      : image_(std::move(image))
      , view_(d.srv_pool().allocate()) {
    d.get()->CreateShaderResourceView(image_, nullptr, view_.cpu());
  }

  ID3D12Resource* resource() const {
    return image_;
  }

  DescriptorHandle const& view() const {
    return view_;
  }

 private:
  friend void load(Image&, ResourceCreator&, char const*);
  CComPtr<ID3D12Resource> image_;
  DescriptorHandle view_;
};

class RenderableImage : noncopyable {
 public:
  RenderableImage() = default;
  RenderableImage(Device& d, Image&& image)
      : image_(std::move(image))
      , target_view_(d.rtv_pool().allocate()) {
    d.get()->CreateRenderTargetView(image_.resource(), nullptr, target_view_.cpu());
  }

  RenderableImage(Device& d, CComPtr<ID3D12Resource> image)
      : image_(d, std::move(image))
      , target_view_(d.rtv_pool().allocate()) {
    d.get()->CreateRenderTargetView(image_.resource(), nullptr, target_view_.cpu());
  }

  RenderableImage(
      CComPtr<ID3D12Resource> image,
      DescriptorHandle&& view,
      DescriptorHandle&& target_view)
      : image_(std::move(image), std::move(view))
      , target_view_(std::move(target_view)) {
  }

  ID3D12Resource* resource() const {
    return image_.resource();
  }

  Image const& image() const {
    return image_;
  }

  DescriptorHandle const& view() const {
    return target_view_;
  }

 private:
  Image image_;
  DescriptorHandle target_view_;
};

class DepthImage : noncopyable {
 public:
  DepthImage() = default;

  DepthImage(Device& d, CComPtr<ID3D12Resource> image)
      : image_(std::move(image))
      , ds_view_(d.dsv_pool().allocate()) {
    // D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
    // desc.Format = DXGI_FORMAT_D32_FLOAT;
    // desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    // desc.Flags = D3D12_DSV_FLAG_NONE;
    d.get()->CreateDepthStencilView(image_, nullptr, ds_view_.cpu());
  }

  DepthImage(CComPtr<ID3D12Resource> image, DescriptorHandle&& ds_view)
      : image_(std::move(image))
      , ds_view_(std::move(ds_view)) {
  }

  ID3D12Resource* resource() const {
    return image_;
  }

  DescriptorHandle const& view() const {
    return ds_view_;
  }

 private:
  CComPtr<ID3D12Resource> image_;
  DescriptorHandle ds_view_;
};

class Model : noncopyable {
 public:
  Model() = default;
  D3D12_VERTEX_BUFFER_VIEW const& view() const {
    return view_;
  }

  std::size_t vertex_count() const {
    return view_.SizeInBytes / view_.StrideInBytes;
  }

 private:
  friend void load(Model&, ResourceCreator&, char const*);
  CComPtr<ID3D12Resource> vertex_buffer_;
  D3D12_VERTEX_BUFFER_VIEW view_;
};

class RenderContext : noncopyable {
 public:
  void target(RenderableImage const& target) {
    target_ = &target;
  }

  void depth(DepthImage const& depth) {
    depth_ = &depth;
  }

  void viewport(int width, int height) {
    viewport_.Width = width;
    viewport_.Height = height;
    viewport_.TopLeftY = 0.0f;
    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.MinDepth = D3D12_MIN_DEPTH;
    viewport_.MaxDepth = D3D12_MAX_DEPTH;
  }

  void scissor(int left, int right, int top, int bottom) {
    scissor_.left = left;
    scissor_.right = right;
    scissor_.top = top;
    scissor_.bottom = bottom;
  }

  void begin_rendering(SubmissionContext& sc, glm::vec4 const& clear_colour) {
    auto* command_list = sc.command_list();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = target_->resource();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    command_list->ResourceBarrier(1, &barrier);

    command_list->RSSetViewports(1, &viewport_);
    command_list->RSSetScissorRects(1, &scissor_);
    command_list->OMSetRenderTargets(
        1,
        &target_->view().cpu(),
        FALSE,
        &depth_->view().cpu());

    command_list->ClearRenderTargetView(
        target_->view().cpu(),
        &clear_colour[0],
        0,
        nullptr);

    if(depth_) {
      command_list->ClearDepthStencilView(
          depth_->view().cpu(),
          D3D12_CLEAR_FLAG_DEPTH,
          1.f,
          0,
          0,
          nullptr);
    }

  }

  void finish_rendering(SubmissionContext& sc) {
    auto* command_list = sc.command_list();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = target_->resource();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    command_list->ResourceBarrier(1, &barrier);
  }

 private:
  RenderableImage const* target_ = nullptr;
  DepthImage const* depth_ = nullptr;
  D3D12_VIEWPORT viewport_;
  D3D12_RECT scissor_;
};

struct StbiImageFree {
  void operator()(void* ptr) {
    stbi_image_free(ptr);
  }
};

void load(Image& image, ResourceCreator& rc, char const* path) {
  auto* device = rc.device().get();
  int width, height, channels;
  std::unique_ptr<stbi_uc, StbiImageFree> pixels(
      stbi_load(path, &width, &height, &channels, STBI_rgb_alpha));

  auto image_resource = rc.create_image_resource(width, height);
  auto staging_resouce = rc.create_staging_resource(image_resource);

  D3D12_SUBRESOURCE_DATA texture_data = {};
  texture_data.pData = pixels.get();
  texture_data.RowPitch = width * 4;
  texture_data.SlicePitch = width * height * 4;
  rc.update_subresources(image_resource, staging_resouce, 0, 0, 1, &texture_data);
  rc.on_finalise([&image,
                  image_resource = std::move(image_resource),
                  staging_resouce = std::move(staging_resouce)](
                     ResourceCreator& rc,
                     SubmissionContext& sc) mutable {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = image_resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    sc.command_list()->ResourceBarrier(1, &barrier);
    image.view_ = rc.device().srv_pool().allocate();
    auto* device = rc.device().get();
    device->CreateShaderResourceView(image_resource, nullptr, image.view_.cpu());
    image.image_ = std::move(image_resource);
  });
}

void load(Model& model, ResourceCreator& rc, char const* path) {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  if(!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path)) {
    throw_error(-1);
  }

  // We only support one vertex format for now.
  struct Vertex {
    glm::vec3 position;
    glm::vec2 uv;
  };

  std::vector<Vertex> vertices;
  // std::vector<std::uint32_t> indices;
  for(auto const& shape : shapes) {
    for(auto const& index : shape.mesh.indices) {
      vertices.emplace_back();
      Vertex& vertex = vertices.back();
      vertex.position = {
          attrib.vertices[3 * index.vertex_index + 0],
          attrib.vertices[3 * index.vertex_index + 1],
          attrib.vertices[3 * index.vertex_index + 2]};
      vertex.uv = {
          attrib.texcoords[2 * index.texcoord_index + 0],
          // obj format puts 0, 0 at bottom left, need to flip it.
          1.f - attrib.texcoords[2 * index.texcoord_index + 1]};
      // Skip the index buffer for now.
      // indices.push_back(indices.size());
    }
  }

  auto vertex_buffer = rc.create_vertex_buffer_resource(
      vertices.size(),
      sizeof(Vertex));
  auto staging_resource = rc.create_staging_resource(vertex_buffer);

  D3D12_SUBRESOURCE_DATA vertex_data = {};
  vertex_data.pData = vertices.data();
  vertex_data.RowPitch = vertices.size() * sizeof(Vertex);
  vertex_data.SlicePitch = vertex_data.RowPitch;
  rc.update_subresources(vertex_buffer, staging_resource, 0, 0, 1, &vertex_data);
  rc.on_finalise([&model,
                  vertex_buffer = std::move(vertex_buffer),
                  staging_resource = std::move(staging_resource),
                  data_size = vertices.size() * sizeof(Vertex)](
                     ResourceCreator& rc,
                     SubmissionContext& sc) mutable {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = vertex_buffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    sc.command_list()->ResourceBarrier(1, &barrier);
    model.view_.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
    model.view_.StrideInBytes = sizeof(Vertex);
    model.view_.SizeInBytes = data_size;
    model.vertex_buffer_ = std::move(vertex_buffer);
  });
}

class Swapchain : noncopyable {
 public:
  Swapchain(Device& device, Window& window, int num_images)
      : device_(device)
      , window_(window)
      , num_images_(num_images) {
    create_swapchain();
    create_images();
    create_fence();
  }

  ~Swapchain() {
    if(present_fence_event_) {
      CloseHandle(present_fence_event_);
    }

    if(swapchain_waitable_) {
      CloseHandle(swapchain_waitable_);
    }
  }

  int image_count() const {
    return num_images_;
  }

  UINT get_current_image_index() const {
    return swapchain_->GetCurrentBackBufferIndex();
  }

  RenderableImage const& target(int idx) const {
    return target_[idx];
  }

  void present(SubmissionContext& sc) {
    swapchain_->Present(1, 0); // 1=With vsync
    UINT64 signal_value = ++current_present_fence_value_;
    throw_error(device_.graphics_queue()->Signal(present_fence_, signal_value));
    sc.present_fence_value(signal_value);
  }

  void wait(SubmissionContext& sc) {
    auto completed_value = present_fence_->GetCompletedValue();
    auto fence_value = sc.present_fence_value();
    if(completed_value < fence_value) {
      throw_error(
          present_fence_->SetEventOnCompletion(fence_value, present_fence_event_));
      WaitForSingleObject(present_fence_event_, INFINITE);
    }
  }

  void wait_for_last_frame() {
    auto completed_value = present_fence_->GetCompletedValue();
    auto fence_value = current_present_fence_value_;

    // Can be zero if no fence is signaled.
    if(completed_value < fence_value) {
      throw_error(
          present_fence_->SetEventOnCompletion(fence_value, present_fence_event_));
      WaitForSingleObject(present_fence_event_, INFINITE);
      LOG(Info) << "Waited for value: " << present_fence_->GetCompletedValue();
    }
  }

  void resize_swapchain(int width, int height) {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    swapchain_->GetDesc1(&desc);
    desc.Width = width;
    desc.Height = height;
    swapchain_ = nullptr;
    for(RenderableImage& i : target_) {
      i = {};
    }

    CloseHandle(swapchain_waitable_);
    create_swapchain_impl(desc);
    create_images();
  }

 private:
  void create_swapchain() {
    DXGI_SWAP_CHAIN_DESC1 sc_desc = {};
    sc_desc.BufferCount = num_images_;
    sc_desc.Width = window_.width();
    sc_desc.Height = window_.height();
    sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc_desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.SampleDesc.Quality = 0;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sc_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    sc_desc.Scaling = DXGI_SCALING_STRETCH;
    sc_desc.Stereo = FALSE;

    create_swapchain_impl(sc_desc);
  }

  void create_swapchain_impl(DXGI_SWAP_CHAIN_DESC1 const& desc) {
    CComPtr<IDXGIFactory4> factory;
    throw_error(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    CComPtr<IDXGISwapChain1> swapchain;
    throw_error(factory->CreateSwapChainForHwnd(
        device_.graphics_queue(),
        window_.hwnd(),
        &desc,
        nullptr,
        nullptr,
        &swapchain));

    throw_error(swapchain->QueryInterface(&swapchain_));
    swapchain_->SetMaximumFrameLatency(num_images_);
    swapchain_waitable_ = swapchain_->GetFrameLatencyWaitableObject();
  }

  void create_images() {
    auto* device = device_.get();
    for(int i = 0; i < num_images_; ++i) {
      CComPtr<ID3D12Resource> image;
      swapchain_->GetBuffer(i, IID_PPV_ARGS(&image));
      target_[i] = {device_, std::move(image)};
    }
  }

  void create_fence() {
    throw_error(device_.get()->CreateFence(
        0,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&present_fence_)));
    current_present_fence_value_ = 1;
    present_fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if(!present_fence_event_) {
      throw_error(HRESULT_FROM_WIN32(GetLastError()));
    }
  }

  Device& device_;
  Window& window_;
  int num_images_ = 0;
  int width_ = 0;
  int height_ = 0;
  CComPtr<IDXGISwapChain3> swapchain_;
  CComPtr<ID3D12Fence> present_fence_;
  UINT64 current_present_fence_value_ = 0;
  HANDLE present_fence_event_ = nullptr;
  HANDLE swapchain_waitable_ = nullptr;
  std::array<RenderableImage, 3> target_;
};

class ImGuiState : noncopyable {
 public:
  ImGuiState(Device& device, Window& window, int num_swapchain_images)
      : device_(device) {
    create_image(window.width(), window.height());

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable
    // Keyboard Controls io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    // // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();

    // Hijjacking vulkan glfw for dx12 seems to work
    ImGui_ImplGlfw_InitForVulkan(window.glfw(), true);

    font_view_ = device_.srv_pool().allocate();

    // Setup Platform/Renderer backends
    ImGui_ImplDX12_Init(
        device.get(),
        num_swapchain_images,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        device_.srv_pool().heap(),
        font_view_.cpu(),
        font_view_.gpu());
  }

  ~ImGuiState() {
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }

  void create_image(int width, int height) {
    D3D12_RESOURCE_DESC desc = {};
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Width = width;
    desc.Height = height;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    desc.DepthOrArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    auto* device = device_.get();

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::copy(&clear_colour_.x, &clear_colour_.x + 4, &clear.Color[0]);

    CComPtr<ID3D12Resource> image;
    throw_error(device->CreateCommittedResource(
        &device_.resource_heap(),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clear,
        IID_PPV_ARGS(&image)));

    target_ = RenderableImage(device_, std::move(image));
  }

  void update() {
    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

  void render(SubmissionContext& sc) {
    ImGui::Render();
    auto command_list = sc.command_list();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = target_.resource();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    command_list->ResourceBarrier(1, &barrier);

    command_list->ClearRenderTargetView(target_.view().cpu(), &clear_colour_.x, 0, nullptr);

    command_list->OMSetRenderTargets(1, &target_.view().cpu(), FALSE, nullptr);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command_list->ResourceBarrier(1, &barrier);
  }

  RenderableImage const& target() const {
    return target_;
  }

 private:
  Device& device_;
  DescriptorHandle render_view_;
  DescriptorHandle font_view_;
  bool show_demo_window_ = false;
  bool show_another_window_ = false;
  glm::vec4 clear_colour_ = {0.f, 0.f, 0.f, 1.f};
  RenderableImage target_;
};

class ForwardDraw : noncopyable {
 public:
  ForwardDraw(
      Device& d,
      VertexShaderHandle const& vertex_shader,
      FragmentShaderHandle const& pixel_shader) {
    create_root_signature(d);
    create_pipeline(d, vertex_shader, pixel_shader);
  }

  void draw(
      SubmissionContext& sc,
      Model const& model,
      Image const& image,
      ShaderData const& shader_data) {
    auto* command_list = sc.command_list();
    command_list->SetGraphicsRootSignature(root_signature_);
    command_list->SetGraphicsRootDescriptorTable(0, shader_data.view().gpu());
    command_list->SetGraphicsRootDescriptorTable(1, image.view().gpu());
    command_list->SetPipelineState(pipeline_);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->IASetVertexBuffers(0, 1, &model.view());
    command_list->DrawInstanced(model.vertex_count(), 1, 0, 0);
  }

 private:
  void create_root_signature(Device& d) {
    std::array<D3D12_DESCRIPTOR_RANGE, 2> descriptor_range;
    descriptor_range[0].NumDescriptors = 1;
    descriptor_range[0].BaseShaderRegister = 0;
    descriptor_range[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    descriptor_range[0].RegisterSpace = 0;
    descriptor_range[1].NumDescriptors = 1;
    descriptor_range[1].BaseShaderRegister = 0;
    descriptor_range[1].OffsetInDescriptorsFromTableStart = 0;
    descriptor_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range[1].RegisterSpace = 0;

    std::array<D3D12_ROOT_DESCRIPTOR_TABLE, 2> descriptor_table;
    descriptor_table[0].NumDescriptorRanges = 1;
    descriptor_table[0].pDescriptorRanges = &descriptor_range[0];
    descriptor_table[1].NumDescriptorRanges = 1;
    descriptor_table[1].pDescriptorRanges = &descriptor_range[1];

    std::array<D3D12_ROOT_PARAMETER, 2> root_parameters = {};
    root_parameters[0].DescriptorTable = descriptor_table[0];
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].DescriptorTable = descriptor_table[1];
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 0;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.NumParameters = root_parameters.size();
    desc.pParameters = root_parameters.data();
    CComPtr<ID3DBlob> signature;
    CComPtr<ID3DBlob> error;
    auto* device = d.get();
    throw_error(D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error));
    throw_error(device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&root_signature_)));
  }

  void create_pipeline(
      Device& d,
      VertexShaderHandle const& vs,
      FragmentShaderHandle const& fs) {
    // We currently only support one vertex format.
    // clang-format off
    std::array<D3D12_INPUT_ELEMENT_DESC, 2> vertex_layout {{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    }};
    // clang-format on

    D3D12_SHADER_BYTECODE vs_bytecode = {};
    vs_bytecode.pShaderBytecode = vs.code()->GetBufferPointer();
    vs_bytecode.BytecodeLength = vs.code()->GetBufferSize();

    D3D12_SHADER_BYTECODE ps_bytecode = {};
    ps_bytecode.pShaderBytecode = fs.code()->GetBufferPointer();
    ps_bytecode.BytecodeLength = fs.code()->GetBufferSize();

    D3D12_RASTERIZER_DESC raster_desc = {};
    raster_desc.FillMode = D3D12_FILL_MODE_SOLID;
    raster_desc.CullMode = D3D12_CULL_MODE_NONE;
    raster_desc.FrontCounterClockwise = FALSE;
    raster_desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    raster_desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    raster_desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    raster_desc.DepthClipEnable = TRUE;
    raster_desc.MultisampleEnable = FALSE;
    raster_desc.AntialiasedLineEnable = FALSE;
    raster_desc.ForcedSampleCount = 0;
    raster_desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_BLEND_DESC blend_desc = {};
    blend_desc.AlphaToCoverageEnable = FALSE;
    blend_desc.IndependentBlendEnable = FALSE;
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].LogicOpEnable = FALSE;
    blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCILOP_DESC dsop_desc = {};
    dsop_desc.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    dsop_desc.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    dsop_desc.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    dsop_desc.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    D3D12_DEPTH_STENCIL_DESC ds_desc = {};
    ds_desc.DepthEnable = TRUE;
    ds_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    ds_desc.StencilEnable = FALSE;
    // ds_desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    // ds_desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    // ds_desc.FrontFace = dsop_desc;
    // ds_desc.BackFace = dsop_desc;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.InputLayout = {vertex_layout.data(), vertex_layout.size()};
    pso_desc.pRootSignature = root_signature_;
    pso_desc.VS = vs_bytecode;
    pso_desc.PS = ps_bytecode;
    pso_desc.RasterizerState = raster_desc;
    pso_desc.BlendState = blend_desc;
    pso_desc.DepthStencilState = ds_desc;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.SampleDesc.Count = 1;

    auto* device = d.get();
    throw_error(
        device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_)));
  }

  CComPtr<ID3D12RootSignature> root_signature_;
  CComPtr<ID3D12PipelineState> pipeline_;
};

class ScreenSpaceDraw : noncopyable {
 public:
  ScreenSpaceDraw(
      Device& d,
      VertexShaderHandle const& vertex_shader,
      FragmentShaderHandle const& pixel_shader) {
    create_root_signature(d);
    create_pipeline(d, vertex_shader, pixel_shader);
  }

  void draw(SubmissionContext& sc, Image const& image) {
    auto* command_list = sc.command_list();
    command_list->SetGraphicsRootSignature(root_signature_);
    command_list->SetGraphicsRootDescriptorTable(0, image.view().gpu());
    command_list->SetPipelineState(pipeline_);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->DrawInstanced(3, 1, 0, 0);
  }

 private:
  void create_root_signature(Device& d) {
    D3D12_DESCRIPTOR_RANGE descriptor_range = {};
    descriptor_range.NumDescriptors = 1;
    descriptor_range.BaseShaderRegister = 0;
    descriptor_range.OffsetInDescriptorsFromTableStart = 0;
    descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range.RegisterSpace = 0;

    D3D12_ROOT_DESCRIPTOR_TABLE descriptor_table = {};
    descriptor_table.NumDescriptorRanges = 1;
    descriptor_table.pDescriptorRanges = &descriptor_range;

    D3D12_ROOT_PARAMETER root_parameters = {};
    root_parameters.DescriptorTable = descriptor_table;
    root_parameters.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_parameters.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 0;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.NumParameters = 1;
    desc.pParameters = &root_parameters;
    CComPtr<ID3DBlob> signature;
    CComPtr<ID3DBlob> error;
    auto* device = d.get();
    throw_error(D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error));
    throw_error(device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&root_signature_)));
  }

  void create_pipeline(
      Device& d,
      VertexShaderHandle const& vs,
      FragmentShaderHandle const& fs) {
    D3D12_SHADER_BYTECODE vs_bytecode = {};
    vs_bytecode.pShaderBytecode = vs.code()->GetBufferPointer();
    vs_bytecode.BytecodeLength = vs.code()->GetBufferSize();

    D3D12_SHADER_BYTECODE ps_bytecode = {};
    ps_bytecode.pShaderBytecode = fs.code()->GetBufferPointer();
    ps_bytecode.BytecodeLength = fs.code()->GetBufferSize();

    D3D12_RASTERIZER_DESC raster_desc = {};
    raster_desc.FillMode = D3D12_FILL_MODE_SOLID;
    raster_desc.CullMode = D3D12_CULL_MODE_BACK;
    raster_desc.FrontCounterClockwise = FALSE;
    raster_desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    raster_desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    raster_desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    raster_desc.DepthClipEnable = TRUE;
    raster_desc.MultisampleEnable = FALSE;
    raster_desc.AntialiasedLineEnable = FALSE;
    raster_desc.ForcedSampleCount = 0;
    raster_desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_BLEND_DESC blend_desc = {};
    blend_desc.AlphaToCoverageEnable = FALSE;
    blend_desc.IndependentBlendEnable = FALSE;
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].LogicOpEnable = FALSE;
    blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.InputLayout = {nullptr, 0};
    pso_desc.pRootSignature = root_signature_;
    pso_desc.VS = vs_bytecode;
    pso_desc.PS = ps_bytecode;
    pso_desc.RasterizerState = raster_desc;
    pso_desc.BlendState = blend_desc;
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.DepthStencilState.StencilEnable = FALSE;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc.Count = 1;

    auto* device = d.get();
    throw_error(
        device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_)));
  }

  CComPtr<ID3D12RootSignature> root_signature_;
  CComPtr<ID3D12PipelineState> pipeline_;
};

std::vector<CComPtr<IDXGIAdapter>> get_adapters() {
  CComPtr<IDXGIFactory4> factory;
  throw_error(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

  std::vector<CComPtr<IDXGIAdapter>> adapters;
  CComPtr<IDXGIAdapter> adapter;
  for(int i = 0; factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    adapters.push_back(std::move(adapter));
  }

  return adapters;
}

class Application : noncopyable {
 public:
  Application(Window& window)
      : window_(window) {
    save_adapters();
  }

  bool run() {
    LOG(Info) << "Running render loop";
    Device device(adapters_[adapter_index_]);

    int num_swapchain_images = 2;
    Swapchain swapchain(device, window_, num_swapchain_images);
    DepthImage depth = create_depth_buffer(device);
    ResourceCreator resource_creator(device);
    resource_creator.begin_loading();
    std::vector<SubmissionContext> submission_context_list;

    int num_framesflight = 1;
    for(int i = 0; i < num_framesflight; ++i) {
      submission_context_list.emplace_back(device);
    }

    std::vector<RenderContext> render_context_list;
    render_context_list.resize(swapchain.image_count());
    for(std::size_t i = 0; i < render_context_list.size(); ++i) {
      auto&& rc = render_context_list[i];
      rc.target(swapchain.target(i));
      rc.scissor(0, window_.width(), 0, window_.height());
      rc.viewport(window_.width(), window_.height());
      rc.depth(depth);
    }

    ImGuiState imgui(device, window_, swapchain.image_count());
    ShaderCache<FragmentShaderHandle> fragment_shaders("ps_5_0");
    fragment_shaders.add("fullscreen_quad", "PSMain");
    fragment_shaders.add("fullscreen_quad", "PSMainInv");
    fragment_shaders.add("static_model", "PSMain");
    ShaderCache<VertexShaderHandle> vertex_shaders("vs_5_0");
    vertex_shaders.add("fullscreen_quad", "VSMain");
    vertex_shaders.add("static_model", "VSMain");

    ScreenSpaceDraw copy_image(
        device,
        vertex_shaders.find("fullscreen_quad", "VSMain"),
        fragment_shaders.find("fullscreen_quad", "PSMain"));
    ScreenSpaceDraw copy_image_inv_alpha(
        device,
        vertex_shaders.find("fullscreen_quad", "VSMain"),
        fragment_shaders.find("fullscreen_quad", "PSMainInv"));

    Image face;
    load(face, resource_creator, "assets/textures/test.jpg");

    Model room_model;
    load(room_model, resource_creator, "assets/models/viking_room.obj");

    Image room_image;
    load(room_image, resource_creator, "assets/textures/viking_room.png");
    resource_creator.finish_loading();

    ForwardDraw forward_render(
        device,
        vertex_shaders.find("static_model", "VSMain"),
        fragment_shaders.find("static_model", "PSMain"));

    ShaderData scene_data(device, sizeof(ViewData));

    CComPtr<ID3D12GraphicsCommandList> command_list;
    throw_error(device.get()->CreateCommandList1(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        D3D12_COMMAND_LIST_FLAG_NONE,
        IID_PPV_ARGS(&command_list)));

    std::uint32_t frame_index = 0;
    auto last_frame_time = std::chrono::high_resolution_clock::now();
    while(!glfwWindowShouldClose(window_.glfw())) {
      glfwPollEvents();

      if(handle_window_size(swapchain, render_context_list) ==
         Window::SizeEvent::Changed) {
        imgui.create_image(window_.width(), window_.height());
        depth = create_depth_buffer(device);
      }

      imgui.update();

      if(ImGui::Begin("Adapter Info")) {
        int selected_index = adapter_index_;
        ImGui::Combo("##name", &selected_index, adapter_names_.data());
        if(selected_index != adapter_index_) {
          adapter_index_ = selected_index;
          LOG(Info) << "Adapter switch detected.";
          swapchain.wait_for_last_frame();
          // Return true unwinds the stack, cleaning everythign up, and
          // then calls run again.
          return true;
        }
        ImGui::End();
      }

      if(ImGui::Begin("Scene Settings")) {
        ImGui::ColorEdit3("Clear Colour", &clear_colour_[0]);
        ImGui::Text(
            "Application average %.3f ms/frame (%.1f FPS)",
            1000.0f / ImGui::GetIO().Framerate,
            ImGui::GetIO().Framerate);
        ImGui::End();
      }

      auto current = std::chrono::high_resolution_clock::now();
      float dT = std::chrono::duration<float>(current - last_frame_time).count();
      dT = std::clamp(dT, 0.01f, 0.05f);
      last_frame_time = current;
      update_render(dT);
      scene_data.write(&main_camera_, sizeof(main_camera_));

      std::uint32_t next_frame_index = frame_index++;
      SubmissionContext& submission_context =
          submission_context_list[next_frame_index % submission_context_list.size()];

      RenderContext& render_context =
          render_context_list[swapchain.get_current_image_index()];

      swapchain.wait(submission_context);
      submission_context.begin_frame();
      submission_context.begin_rendering(command_list);
      resource_creator.finalise_all(submission_context);
      imgui.render(submission_context);
      render_context.begin_rendering(submission_context, clear_colour_);
      // copy_image.draw(submission_context, face);
      forward_render.draw(submission_context, room_model, room_image, scene_data);
      copy_image_inv_alpha.draw(submission_context, imgui.target().image());
      render_context.finish_rendering(submission_context);
      submission_context.finish_rendering();
      swapchain.present(submission_context);
    }

    swapchain.wait_for_last_frame();
    return false;
  }

 private:
  struct alignas(256) ViewData {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model = glm::mat4(1.f);
  };

  void update_render(float dT) {
    main_camera_.model = glm::rotate(
        main_camera_.model,
        dT * glm::radians(90.0f),
        glm::vec3(0.0f, 0.0f, 1.0f));
    main_camera_.view = glm::lookAt(
        glm::vec3(2.0f, 2.0f, 2.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f));
    main_camera_.projection = glm::perspective(
        glm::radians(45.0f),
        // Should actually come from the swapchain.
        window_.width() / (window_.height() * 1.f),
        0.1f,
        10.0f);
  }

  void save_adapters() {
    adapters_ = get_adapters();
    for(auto&& adapter : adapters_) {
      DXGI_ADAPTER_DESC desc = {};
      adapter->GetDesc(&desc);
      auto narrow_name = utf16_to_utf8(desc.Description);
      adapter_names_.insert(
          adapter_names_.end(),
          narrow_name.begin(),
          narrow_name.end());
      adapter_names_.push_back('\0');
    }

    adapter_names_.push_back('\0');
  }

  Window::SizeEvent handle_window_size(
      Swapchain& swapchain,
      std::vector<RenderContext>& render_context_list) {
    if(window_.handle_window_size() == Window::SizeEvent::Changed) {
      swapchain.resize_swapchain(window_.width(), window_.height());
      render_context_list.clear();
      render_context_list.resize(swapchain.image_count());
      for(std::size_t i = 0; i < render_context_list.size(); ++i) {
        auto&& rc = render_context_list[i];
        rc.target(swapchain.target(i));
        rc.scissor(0, window_.width(), 0, window_.height());
        rc.viewport(window_.width(), window_.height());
      }
      return Window::SizeEvent::Changed;
    }
    return Window::SizeEvent::None;
  }

  DepthImage create_depth_buffer(Device& d) const {
    D3D12_RESOURCE_DESC desc = {};
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.Width = window_.width();
    desc.Height = window_.height();
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    desc.DepthOrArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    auto* device = d.get();

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.f;
    clear.DepthStencil.Stencil = 0;
    CComPtr<ID3D12Resource> image;
    throw_error(device->CreateCommittedResource(
        &d.resource_heap(),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear,
        IID_PPV_ARGS(&image)));
    return {d, std::move(image)};
  }

  std::vector<CComPtr<IDXGIAdapter>> adapters_;
  int adapter_index_ = 0;
  glm::vec4 clear_colour_ = {0.4f, 0.45f, 0.6f, 1.f};
  std::vector<char> adapter_names_;
  Window& window_;
  ViewData main_camera_;
};

static void glfw_error_callback(int error, const char* description) {
  std::cerr << "Glfw Error " << error << ": " << description;
}

int main(int, char**) {
  struct DxReport {
    ~DxReport() {
#ifdef RNDRX_ENABLE_DX12_DEBUG_LAYER
      CComPtr<IDXGIDebug1> xgi_debug;
      if(SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&xgi_debug)))) {
        xgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
      }
#endif
    }
  } report_on_exit;

  glfwSetErrorCallback(glfw_error_callback);
  if(!glfwInit())
    return 1;

  struct CleanupGlfw {
    ~CleanupGlfw() {
      glfwTerminate();
    }
  } cleanup_glfw;

  Window window;
  Application app(window);
  while(app.run())
    ;

  return 0;
}
