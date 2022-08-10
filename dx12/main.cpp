// Copyright (c) 2022 Google Inc.
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
#define NOMINMAX                           1
#define WIN32_LEAN_AND_MEAN                1
#define GLM_FORCE_RADIANS                  1
#define GLM_FORCE_DEPTH_ZERO_TO_ONE        1
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES 1
#define TINYOBJLOADER_IMPLEMENTATION       1
#define STB_IMAGE_IMPLEMENTATION           1
#define GLFW_EXPOSE_NATIVE_WIN32           1

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <Windows.h>
#include <atlbase.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <stb_image.h>
#include <tiny_obj_loader.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/fast_trigonometry.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <iostream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_glfw.h"
#include "pso_caching.h"

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

template <typename Ex>
void throw_exception(Ex e) {
  throw e;
}

void throw_runtime_error(char const* format) {
  throw_exception(std::runtime_error(format));
}

void check_hr(HRESULT r, std::string_view message = "unknown failure") {
  if(FAILED(r)) {
    int err = ::GetLastError();
    std::stringstream msg;
    msg << "HRESULT: " << message << " (Error: hr=" << r
        << ", GetLastError=" << err << ")";
    throw_exception(std::runtime_error(msg.str()));
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
    window_ = glfwCreateWindow(
        width_,
        height_,
        "rndrx-dx12",
        /* glfwGetPrimaryMonitor() */ nullptr,
        nullptr);
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
    check_hr(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_)));
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
      check_hr(D3D12CreateDevice(adapter, featureLevel, IID_PPV_ARGS(&ptr)));

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
    check_hr(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&graphics_queue_)));
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
    check_hr(device_.get()->CreateCommandAllocator(
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
    check_hr(command_allocator_->Reset());
  }

  void begin_rendering(ID3D12GraphicsCommandList* command_list) {
    command_list_ = command_list;
    command_list_->Reset(command_allocator_, nullptr);
    std::array<ID3D12DescriptorHeap*, 1> heaps = {device_.srv_pool().heap()};
    command_list_->SetDescriptorHeaps(heaps.size(), heaps.data());
  }

  void finish_rendering() {
    check_hr(command_list_->Close());
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
      wait();
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
    check_hr(device_.get()->CreateCommittedResource(
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
    check_hr(device_.get()->CreateCommittedResource(
        &device_.resource_heap(),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&vertex_buffer)));

    return vertex_buffer;
  }

  void reset() {
    check_hr(command_allocator_->Reset());
  }

  void begin_loading() {
    command_list_->Reset(command_allocator_, nullptr);
  }

  void finish_loading() {
    check_hr(command_list_->Close());
    std::array<ID3D12CommandList*, 1> commands = {command_list_};
    copy_queue_->ExecuteCommandLists(commands.size(), commands.data());
    std::uint64_t signal_value = ++current_fence_value_;
    check_hr(copy_queue_->Signal(copy_fence_, signal_value));
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
      completed_value = current_fence_value_;
      check_hr(
          copy_fence_->SetEventOnCompletion(completed_value, copy_fence_event_));
      WaitForSingleObject(copy_fence_event_, INFINITE);
    }

    while(!finalisation_queue_.empty() &&
          finalisation_queue_.front().fence_value <= completed_value) {
      finalisation_queue_.front().fn(*this, sc);
      finalisation_queue_.pop();
    }
  }

  void wait() {
    auto completed_value = copy_fence_->GetCompletedValue();
    if(completed_value < current_fence_value_) {
      completed_value = current_fence_value_;
      check_hr(
          copy_fence_->SetEventOnCompletion(completed_value, copy_fence_event_));
      WaitForSingleObject(copy_fence_event_, INFINITE);
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
    check_hr(device->CreateCommittedResource(
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
    check_hr(staging->Map(0, &read_range, reinterpret_cast<void**>(&staging_mem)));
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
    check_hr(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_COPY,
        IID_PPV_ARGS(&command_allocator_)));

    check_hr(device->CreateCommandList1(
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
    check_hr(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&copy_queue_)));
  }

  void create_fence() {
    auto* device = device_.get();
    check_hr(
        device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copy_fence_)));
    copy_fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if(!copy_fence_event_) {
      check_hr(HRESULT_FROM_WIN32(GetLastError()));
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

class ShaderMetadata {
 public:
  ShaderMetadata(ID3D12ShaderReflection* meta)
      : meta_(meta) {
    cache_descriptor_ranges();
  }

  std::vector<D3D12_DESCRIPTOR_RANGE> const& get_descriptor_ranges() const {
    return descriptor_ranges_;
  }

  std::vector<D3D12_DESCRIPTOR_RANGE> const& get_sampler_ranges() const {
    return sampler_ranges_;
  }

 private:
  void cache_descriptor_ranges() {
    D3D12_SHADER_DESC vs_desc = {};
    meta_->GetDesc(&vs_desc);
    for(unsigned idx = 0; idx < vs_desc.BoundResources; ++idx) {
      D3D12_SHADER_INPUT_BIND_DESC binding_desc;
      meta_->GetResourceBindingDesc(idx, &binding_desc);
      if(binding_desc.Type == D3D_SIT_CBUFFER) {
        descriptor_ranges_.emplace_back();
        D3D12_DESCRIPTOR_RANGE& range = descriptor_ranges_.back();
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        range.BaseShaderRegister = binding_desc.BindPoint;
        range.NumDescriptors = 1;
        range.RegisterSpace = 0;
      }
      else if(binding_desc.Type == D3D_SIT_TEXTURE) {
        descriptor_ranges_.emplace_back();
        D3D12_DESCRIPTOR_RANGE& range = descriptor_ranges_.back();
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        range.BaseShaderRegister = binding_desc.BindPoint;
        range.NumDescriptors = 1;
        range.RegisterSpace = 0;
      }
      else if(binding_desc.Type == D3D_SIT_SAMPLER) {
        sampler_ranges_.emplace_back();
        D3D12_DESCRIPTOR_RANGE& range = sampler_ranges_.back();
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        range.BaseShaderRegister = binding_desc.BindPoint;
        range.NumDescriptors = 1;
        range.RegisterSpace = 0;
      }
    }
  }

  ID3D12ShaderReflection* meta_;
  std::vector<D3D12_DESCRIPTOR_RANGE> descriptor_ranges_;
  std::vector<D3D12_DESCRIPTOR_RANGE> sampler_ranges_;
};

template <typename>
class ShaderCache;

class FragmentShaderHandle : noncopyable {
 public:
  IDxcBlob* code() const {
    return code_;
  }

  std::vector<D3D12_ROOT_DESCRIPTOR_TABLE> const& get_descriptor_tables() const {
    return descriptor_tables_;
  }

 private:
  friend class ShaderCache<FragmentShaderHandle>;
  FragmentShaderHandle(IDxcBlob* shader, ID3D12ShaderReflection* meta)
      : code_(shader)
      , meta_(meta) {
    cache_descriptor_tables();
  }

  void cache_descriptor_tables() {
    auto&& ranges = meta_.get_descriptor_ranges();
    std::transform(
        ranges.begin(),
        ranges.end(),
        std::back_inserter(descriptor_tables_),
        [](D3D12_DESCRIPTOR_RANGE const& range) -> D3D12_ROOT_DESCRIPTOR_TABLE {
          return {1, &range};
        });
  }

  IDxcBlob* code_;
  ShaderMetadata meta_;
  std::vector<D3D12_ROOT_DESCRIPTOR_TABLE> descriptor_tables_;
};

class VertexShaderHandle : noncopyable {
 public:
  IDxcBlob* code() const {
    return code_;
  }

  std::vector<D3D12_ROOT_DESCRIPTOR_TABLE> const& get_descriptor_tables() const {
    return descriptor_tables_;
  }

 private:
  friend class ShaderCache<VertexShaderHandle>;
  VertexShaderHandle(IDxcBlob* shader, ID3D12ShaderReflection* meta)
      : code_(shader)
      , meta_(meta) {
    cache_descriptor_tables();
  }

  void cache_descriptor_tables() {
    auto&& ranges = meta_.get_descriptor_ranges();
    std::transform(
        ranges.begin(),
        ranges.end(),
        std::back_inserter(descriptor_tables_),
        [](D3D12_DESCRIPTOR_RANGE const& range) -> D3D12_ROOT_DESCRIPTOR_TABLE {
          return {1, &range};
        });
  }

  IDxcBlob* code_;
  ShaderMetadata meta_;
  std::vector<D3D12_ROOT_DESCRIPTOR_TABLE> descriptor_tables_;
};

class ShaderCompiler {
 public:
  ShaderCompiler() {
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils_));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_));
  }

  CComPtr<IDxcUtils> const& utils() const {
    return utils_;
  }
  CComPtr<IDxcCompiler3> const& compiler() const {
    return compiler_;
  }

 private:
  CComPtr<IDxcUtils> utils_;
  CComPtr<IDxcCompiler3> compiler_;
};

template <typename ShaderHandle>
class ShaderCache {
 public:
  ShaderCache(std::string_view shader_model)
      : shader_model_(shader_model.begin(), shader_model.end()) {
  }

  ShaderHandle compile(ShaderCompiler const& sc, std::string file, std::string entry) {
    std::wstring wfile(file.begin(), file.end());
    std::wstring wentry(entry.begin(), entry.end());
    std::wstring path = L"assets/shaders/";
    path += wfile + L".hlsl";

#if RNDRX_ENABLE_SHADER_DEBUGGING
    std::wstring_view optimization_option = L"-Od";
    std::wstring_view debug_info_option = L"-Zs";
#else
    std::wstring_view optimization_option = L"-O3";
    std::wstring_view debug_info_option = L"";
#endif

    std::array<LPCWSTR, 7> args = {
        path.c_str(),
        L"-E",
        wentry.c_str(),
        L"-T",
        shader_model_.c_str(),
        debug_info_option.data(),
        optimization_option.data()};

    CComPtr<IDxcBlobEncoding> source;
    check_hr(sc.utils()->LoadFile(path.c_str(), nullptr, &source));
    DxcBuffer source_buffer;
    source_buffer.Ptr = source->GetBufferPointer();
    source_buffer.Size = source->GetBufferSize();
    source_buffer.Encoding = DXC_CP_ACP;

    CComPtr<IDxcResult> result;
    check_hr(sc.compiler()->Compile(
        &source_buffer,
        args.data(),
        args.size(),
        nullptr,
        IID_PPV_ARGS(&result)));

    CComPtr<IDxcBlobUtf8> errors;
    check_hr(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr));
    if(errors && errors->GetStringLength() != 0) {
      OutputDebugStringA(errors->GetStringPointer());
    }

    HRESULT hr_status;
    result->GetStatus(&hr_status);
    check_hr(hr_status);

    CComPtr<IDxcBlob> code;
    CComPtr<IDxcBlobUtf16> shader_name;
    check_hr(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&code), &shader_name));
    if(!code) {
      throw_runtime_error("Failed to obtain shader binary");
    }

    CComPtr<IDxcBlob> reflection_data;
    check_hr(
        result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflection_data), nullptr));
    if(!reflection_data) {
      throw_runtime_error("Failed to get shader reflection data");
    }

    DxcBuffer reflection_buffer;
    reflection_buffer.Encoding = DXC_CP_ACP;
    reflection_buffer.Ptr = reflection_data->GetBufferPointer();
    reflection_buffer.Size = reflection_data->GetBufferSize();

    CComPtr<ID3D12ShaderReflection> reflection;
    check_hr(
        sc.utils()->CreateReflection(&reflection_buffer, IID_PPV_ARGS(&reflection)));

    auto item = shaders_.emplace(
        ShaderDef(std::move(file), std::move(entry)),
        Shader(std::move(code), std::move(reflection)));
    return {item.first->second.code.p, item.first->second.meta.p};
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

  struct Shader {
    Shader(CComPtr<IDxcBlob>&& c, CComPtr<ID3D12ShaderReflection>&& m)
        : code(std::move(c))
        , meta(std::move(m)) {
    }

    CComPtr<IDxcBlob> code;
    CComPtr<ID3D12ShaderReflection> meta;
  };

  struct HashShaderDef {
    std::size_t operator()(ShaderDef const& sd) const {
      auto hasher = std::hash<std::string>();
      return hasher(sd.file) ^ hasher(sd.entry);
    }
  };

  std::unordered_map<ShaderDef, Shader, HashShaderDef> shaders_;
  std::wstring shader_model_;
};

class ShaderData {
 public:
  ShaderData(Device& d, std::size_t size) {
    create_constant_buffer(d, size);
    create_view(d, size);

    D3D12_RANGE read_range = {0, 0};
    check_hr(
        constant_buffer_->Map(0, &read_range, reinterpret_cast<void**>(&ptr_)));
  }

  DescriptorHandle const& view() const {
    return view_;
  }

  void write(void* source, std::size_t size) {
    write(source, 0, size);
  }

  void write(void* source, std::size_t offset, std::size_t size) {
    std::memcpy(ptr_ + offset, source, size);
  }

  CComPtr<ID3D12Resource> constant_buffer_;

 private:
  void create_constant_buffer(Device& d, std::size_t size) {
    auto* device = d.get();
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = std::max<std::size_t>(size, 256);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    check_hr(device->CreateCommittedResource(
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
    desc.SizeInBytes = std::max<std::size_t>(size, 256);
    view_ = d.srv_pool().allocate();
    device->CreateConstantBufferView(&desc, view_.cpu());
  }

  DescriptorHandle view_;
  char* ptr_;
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

class TargetableImage : noncopyable {
 public:
  TargetableImage() = default;
  TargetableImage(Device& d, Image&& image)
      : image_(std::move(image))
      , target_view_(d.rtv_pool().allocate()) {
    d.get()->CreateRenderTargetView(image_.resource(), nullptr, target_view_.cpu());
  }

  TargetableImage(Device& d, CComPtr<ID3D12Resource> image)
      : image_(d, std::move(image))
      , target_view_(d.rtv_pool().allocate()) {
    d.get()->CreateRenderTargetView(image_.resource(), nullptr, target_view_.cpu());
  }

  TargetableImage(
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

class Geometry : noncopyable {
 public:
  Geometry() = default;
  D3D12_VERTEX_BUFFER_VIEW const& view() const {
    return view_;
  }

  std::size_t vertex_count() const {
    return view_.SizeInBytes / view_.StrideInBytes;
  }

 private:
  friend void load(Geometry&, ResourceCreator&, char const*);
  CComPtr<ID3D12Resource> vertex_buffer_;
  D3D12_VERTEX_BUFFER_VIEW view_;
};

class RenderContext : noncopyable {
 public:
  void target(TargetableImage const& target) {
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
  TargetableImage const* target_ = nullptr;
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

void load(Geometry& model, ResourceCreator& rc, char const* path) {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  if(!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path)) {
    check_hr(-1);
  }

  // We only support one vertex format for now.
  struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
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

      vertex.normal = {
          attrib.normals[3 * index.normal_index + 0],
          attrib.normals[3 * index.normal_index + 1],
          attrib.normals[3 * index.normal_index + 2]};

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
      wait_for_last_frame();
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

  TargetableImage const& target(int idx) const {
    return target_[idx];
  }

  void present(SubmissionContext& sc) {
    swapchain_->Present(0, 0); // 1=With vsync
    UINT64 signal_value = ++current_present_fence_value_;
    check_hr(device_.graphics_queue()->Signal(present_fence_, signal_value));
    sc.present_fence_value(signal_value);
  }

  void wait(SubmissionContext& sc) {
    auto completed_value = present_fence_->GetCompletedValue();
    auto fence_value = sc.present_fence_value();
    if(completed_value < fence_value) {
      check_hr(
          present_fence_->SetEventOnCompletion(fence_value, present_fence_event_));
      WaitForSingleObject(present_fence_event_, INFINITE);
    }
  }

  void wait_for_last_frame() {
    auto completed_value = present_fence_->GetCompletedValue();
    auto fence_value = current_present_fence_value_;

    if(completed_value < fence_value) {
      check_hr(
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
    for(TargetableImage& i : target_) {
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
    check_hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    CComPtr<IDXGISwapChain1> swapchain;
    check_hr(factory->CreateSwapChainForHwnd(
        device_.graphics_queue(),
        window_.hwnd(),
        &desc,
        nullptr,
        nullptr,
        &swapchain));

    check_hr(swapchain->QueryInterface(&swapchain_));
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
    check_hr(device_.get()->CreateFence(
        0,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&present_fence_)));
    present_fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if(!present_fence_event_) {
      check_hr(HRESULT_FROM_WIN32(GetLastError()));
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
  std::array<TargetableImage, 3> target_;
};

class ImGuiState : noncopyable {
 public:
  ImGuiState(Device& device, Window& window, int num_swapchain_images)
      : device_(device) {
    create_image(window.width(), window.height());
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

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
    check_hr(device->CreateCommittedResource(
        &device_.resource_heap(),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clear,
        IID_PPV_ARGS(&image)));

    target_ = TargetableImage(device_, std::move(image));
  }

  void update() {
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

  TargetableImage const& target() const {
    return target_;
  }

 private:
  Device& device_;
  DescriptorHandle render_view_;
  DescriptorHandle font_view_;
  bool show_demo_window_ = false;
  bool show_another_window_ = false;
  glm::vec4 clear_colour_ = {0.f, 0.f, 0.f, 1.f};
  TargetableImage target_;
};

class Model : noncopyable {
 public:
  Model(Geometry const& geo, Image const& albedo, Image const& normals)
      : geometry_(geo)
      , albedo_(albedo)
      , normals_(normals) {
  }

  Image const& albedo() const {
    return albedo_;
  }

  Image const& normals() const {
    return normals_;
  }

  Geometry const& geometry() const {
    return geometry_;
  }

 private:
  Geometry const& geometry_;
  Image const& albedo_;
  Image const& normals_;
};

class DrawModelForward : noncopyable {
 public:
  DrawModelForward(
      Device& d,
      VertexShaderHandle const& vertex_shader,
      FragmentShaderHandle const& pixel_shader) {
    create_root_signature(d, vertex_shader, pixel_shader);
    create_pipeline(d, vertex_shader, pixel_shader);
  }

  void draw(
      SubmissionContext& sc,
      Model const& model,
      ShaderData const& view_data,
      ShaderData const& object_data,
      ShaderData const& lighting_data) {
    auto* command_list = sc.command_list();
    command_list->SetGraphicsRootSignature(root_signature_);
    command_list->SetGraphicsRootDescriptorTable(0, view_data.view().gpu());
    command_list->SetGraphicsRootDescriptorTable(1, object_data.view().gpu());
    command_list->SetGraphicsRootDescriptorTable(2, lighting_data.view().gpu());
    command_list->SetGraphicsRootDescriptorTable(3, model.albedo().view().gpu());
    command_list->SetPipelineState(pipeline_);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->IASetVertexBuffers(0, 1, &model.geometry().view());
    command_list->DrawInstanced(model.geometry().vertex_count(), 1, 0, 0);
  }

 private:
  void create_root_signature(
      Device& d,
      VertexShaderHandle const& vs,
      FragmentShaderHandle const& fs) {
    D3D12_DESCRIPTOR_RANGE1 view_data = {};
    view_data.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    view_data.NumDescriptors = 1;
    view_data.BaseShaderRegister = 0;
    view_data.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    view_data.RegisterSpace = 0;
    view_data.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

    D3D12_DESCRIPTOR_RANGE1 object_data = {};
    object_data.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    object_data.NumDescriptors = 1;
    object_data.BaseShaderRegister = 1;
    object_data.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    object_data.RegisterSpace = 0;
    object_data.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

    D3D12_DESCRIPTOR_RANGE1 albedo_data = {};
    albedo_data.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    albedo_data.NumDescriptors = 1;
    albedo_data.BaseShaderRegister = 0;
    albedo_data.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    albedo_data.RegisterSpace = 0;
    albedo_data.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

    D3D12_DESCRIPTOR_RANGE1 light_data = {};
    light_data.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    light_data.NumDescriptors = 1;
    light_data.BaseShaderRegister = 2;
    light_data.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    light_data.RegisterSpace = 0;
    view_data.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

    std::array<D3D12_ROOT_PARAMETER1, 4> root_parameters = {};
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &view_data;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &object_data;
    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[2].DescriptorTable.pDescriptorRanges = &light_data;
    root_parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_parameters[3].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[3].DescriptorTable.pDescriptorRanges = &albedo_data;

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

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.Desc_1_1.NumStaticSamplers = 1;
    desc.Desc_1_1.pStaticSamplers = &sampler;
    desc.Desc_1_1.NumParameters = root_parameters.size();
    desc.Desc_1_1.pParameters = root_parameters.data();
    CComPtr<ID3DBlob> signature;
    CComPtr<ID3DBlob> error;
    auto* device = d.get();
    if(S_OK != D3D12SerializeVersionedRootSignature(&desc, &signature, &error)) {
      throw_runtime_error(static_cast<char const*>(error->GetBufferPointer()));
    }
    check_hr(device->CreateRootSignature(
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
    std::array<D3D12_INPUT_ELEMENT_DESC, 3> vertex_layout {{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
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

    D3D12_DEPTH_STENCIL_DESC ds_desc = {};
    ds_desc.DepthEnable = TRUE;
    ds_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    ds_desc.StencilEnable = FALSE;

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

#ifdef RNDRX_USE_PSO_CACHING
    create_pso_with_caching(
        d.get(),
        &pso_desc,
        "draw-model-forward",
        vs.code(),
        fs.code(),
        &pipeline_);
#else
    auto* device = d.get();
    check_hr(
        device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_)));
#endif /* RNDRX_USE_PSO_CACHING */
  }

  CComPtr<ID3D12RootSignature> root_signature_;
  CComPtr<ID3D12PipelineState> pipeline_;
};

class DrawDebugGeometry : noncopyable {
 public:
  DrawDebugGeometry(
      Device& d,
      VertexShaderHandle const& vertex_shader,
      FragmentShaderHandle const& pixel_shader) {
    create_root_signature(d, vertex_shader, pixel_shader);
    create_pipeline(d, vertex_shader, pixel_shader);
  }

  void draw(
      SubmissionContext& sc,
      Geometry const& geo,
      ShaderData const& view_data,
      ShaderData const& object_data) {
    auto* command_list = sc.command_list();
    command_list->SetGraphicsRootSignature(root_signature_);
    command_list->SetGraphicsRootDescriptorTable(0, view_data.view().gpu());
    command_list->SetGraphicsRootDescriptorTable(1, object_data.view().gpu());
    command_list->SetPipelineState(pipeline_);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->IASetVertexBuffers(0, 1, &geo.view());
    command_list->DrawInstanced(geo.vertex_count(), 1, 0, 0);
  }

 private:
  void create_root_signature(
      Device& d,
      VertexShaderHandle const& vs,
      FragmentShaderHandle const& fs) {
    D3D12_DESCRIPTOR_RANGE1 view_data = {};
    view_data.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    view_data.NumDescriptors = 1;
    view_data.BaseShaderRegister = 0;
    view_data.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    view_data.RegisterSpace = 0;
    view_data.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

    D3D12_DESCRIPTOR_RANGE1 object_data = {};
    object_data.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    object_data.NumDescriptors = 1;
    object_data.BaseShaderRegister = 1;
    object_data.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    object_data.RegisterSpace = 0;
    object_data.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

    std::array<D3D12_ROOT_PARAMETER1, 2> root_parameters = {};
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &view_data;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &object_data;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.Desc_1_1.NumStaticSamplers = 0;
    desc.Desc_1_1.pStaticSamplers = nullptr;
    desc.Desc_1_1.NumParameters = root_parameters.size();
    desc.Desc_1_1.pParameters = root_parameters.data();
    CComPtr<ID3DBlob> signature;
    CComPtr<ID3DBlob> error;
    auto* device = d.get();
    if(S_OK != D3D12SerializeVersionedRootSignature(&desc, &signature, &error)) {
      throw_runtime_error(static_cast<char const*>(error->GetBufferPointer()));
    }
    check_hr(device->CreateRootSignature(
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
    std::array<D3D12_INPUT_ELEMENT_DESC, 3> vertex_layout {{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    }};
    // clang-format on

    D3D12_SHADER_BYTECODE vs_bytecode = {};
    vs_bytecode.pShaderBytecode = vs.code()->GetBufferPointer();
    vs_bytecode.BytecodeLength = vs.code()->GetBufferSize();

    D3D12_SHADER_BYTECODE ps_bytecode = {};
    ps_bytecode.pShaderBytecode = fs.code()->GetBufferPointer();
    ps_bytecode.BytecodeLength = fs.code()->GetBufferSize();

    D3D12_RASTERIZER_DESC raster_desc = {};
    raster_desc.FillMode = D3D12_FILL_MODE_WIREFRAME;
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

    D3D12_DEPTH_STENCIL_DESC ds_desc = {};
    ds_desc.DepthEnable = TRUE;
    ds_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ds_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    ds_desc.StencilEnable = FALSE;

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

#ifdef RNDRX_USE_PSO_CACHING
    create_pso_with_caching(
        d.get(),
        &pso_desc,
        "draw-debug-geometry",
        vs.code(),
        fs.code(),
        &pipeline_);
#else
    auto* device = d.get();
    check_hr(
        device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_)));
#endif /* RNDRX_USE_PSO_CACHING */
  }

  CComPtr<ID3D12RootSignature> root_signature_;
  CComPtr<ID3D12PipelineState> pipeline_;
};

class DrawImage : noncopyable {
 public:
  DrawImage(
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
    D3D12_DESCRIPTOR_RANGE texture = {};
    texture.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texture.NumDescriptors = 1;

    D3D12_ROOT_PARAMETER root_table = {};
    root_table.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_table.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_table.DescriptorTable.NumDescriptorRanges = 1;
    root_table.DescriptorTable.pDescriptorRanges = &texture;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
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
    desc.pParameters = &root_table;
    CComPtr<ID3DBlob> signature;
    CComPtr<ID3DBlob> error;
    auto* device = d.get();
    check_hr(D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error));
    check_hr(device->CreateRootSignature(
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

#ifdef RNDRX_USE_PSO_CACHING
    create_pso_with_caching(
        d.get(),
        &pso_desc,
        "draw-image",
        vs.code(),
        fs.code(),
        &pipeline_);
#else
    auto* device = d.get();
    check_hr(
        device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_)));
#endif /* RNDRX_USE_PSO_CACHING */
  }

  CComPtr<ID3D12RootSignature> root_signature_;
  CComPtr<ID3D12PipelineState> pipeline_;
};

struct ShaderPointLight {
  glm::vec3 position;
  glm::vec3 colour;
};

class PointLight {
 public:
  PointLight(float radius, float zenith, float azimuth)
      : radius_(radius)
      , zenith_(zenith)
      , azimuth_(azimuth) {
  }

  void update_debug_ui() {
    ImGui::SliderFloat("Radius", &radius_, 0.1f, 50.f);
    ImGui::SliderAngle("Zenith", &zenith_, -360.f, 360.f, "%1.f");
    ImGui::SliderAngle("Azimuth", &azimuth_, -360.f, 360.f, "%1.f");
    ImGui::Checkbox("Enabled", &enabled_);
    ImGui::SliderFloat("Power", &power_, 0, 100);
    ImGui::ColorEdit3("Colour", &colour_.x);
  }

  glm::vec3 position() const {
    return {
        radius_ * glm::cos(zenith_) * glm::cos(azimuth_),
        radius_ * glm::sin(zenith_),
        radius_ * glm::cos(zenith_) * glm::sin(azimuth_)};
  }

  glm::vec3 colour() const {
    return enabled_ ? colour_ * power_ : glm::vec3(0);
  }

 private:
  glm::vec3 colour_{1};
  float radius_ = 10;
  float zenith_ = 0;
  float azimuth_ = 0;
  float power_ = 10;
  bool enabled_ = true;
};

std::vector<CComPtr<IDXGIAdapter>> get_adapters() {
  CComPtr<IDXGIFactory4> factory;
  check_hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

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
    ResourceCreator resource_creator(device);
    resource_creator.begin_loading();

    ShaderCompiler sc;
    ShaderCache<VertexShaderHandle> vertex_shaders("vs_6_0");
    auto fullscreen_vs = vertex_shaders.compile(sc, "fullscreen_quad", "VSMain");
    auto static_model_vs = vertex_shaders.compile(sc, "static_model", "VSMain");

    ShaderCache<FragmentShaderHandle> fragment_shaders("ps_6_0");
    auto fullscreen_ps =
        fragment_shaders.compile(sc, "fullscreen_quad", "PSMain");
    auto fullscreen_ps_inv =
        fragment_shaders.compile(sc, "fullscreen_quad", "PSMainInv");
    auto albedo_ps = fragment_shaders.compile(sc, "static_model", "Albedo");
    auto phong_ps = fragment_shaders.compile(sc, "static_model", "Phong");
    auto debug_ps = fragment_shaders.compile(sc, "static_model", "Debug");

    DrawImage copy_image(device, fullscreen_vs, fullscreen_ps);
    DrawImage copy_image_inv_alpha(device, fullscreen_vs, fullscreen_ps_inv);

    Image background;
    load(background, resource_creator, "assets/textures/background.jpg");

    Geometry main_geometry;
    load(main_geometry, resource_creator, "assets/models/cottage.obj");

    Image main_albedo;
    load(
        main_albedo,
        resource_creator,
        "assets/textures/Cottage_Clean/Cottage_Clean_Base_Color.png");

    Image main_normal;
    load(
        main_normal,
        resource_creator,
        "assets/textures/Cottage_Clean/Cottage_Clean_Normal.png");

    Geometry debug_sphere;
    load(debug_sphere, resource_creator, "assets/models/sphere.obj");

    Model main_model(main_geometry, main_albedo, main_normal);
    resource_creator.finish_loading();

    DrawModelForward forward_render(device, static_model_vs, phong_ps);
    DrawDebugGeometry debug_draw(device, static_model_vs, debug_ps);

    ShaderData view_data(device, sizeof(ViewShaderData));
    ShaderData object_data(device, sizeof(ObjectShaderData));

    std::vector<PointLight> lights;
    lights.emplace_back(10.f, glm::radians(45.f), 0.f);
    lights.emplace_back(10.f, glm::radians(45.f), glm::radians(135.f));
    lights.emplace_back(10.f, glm::radians(45.f), glm::radians(-135.f));
    ShaderData light_data(device, sizeof(ShaderPointLight) * lights.size());
    std::vector<ShaderData> light_positions;
    light_positions.emplace_back(device, sizeof(ObjectShaderData));
    light_positions.emplace_back(device, sizeof(ObjectShaderData));
    light_positions.emplace_back(device, sizeof(ObjectShaderData));

    for(int i = 0; i < lights.size(); ++i) {
      auto&& light = lights[i];
      ShaderPointLight shader_light;
      shader_light.position = light.position();
      shader_light.colour = light.colour();
      light_data.write(
          &shader_light,
          sizeof(ShaderPointLight) * i,
          sizeof(ShaderPointLight));

      glm::mat4 world_light = glm::translate(glm::mat4(1), shader_light.position);
      light_positions[i].write(&world_light, sizeof(world_light));
    }

    CComPtr<ID3D12GraphicsCommandList> command_list;
    check_hr(device.get()->CreateCommandList1(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        D3D12_COMMAND_LIST_FLAG_NONE,
        IID_PPV_ARGS(&command_list)));

    int num_swapchain_images = 3;
    Swapchain swapchain = {device, window_, num_swapchain_images};
    DepthImage depth = create_depth_buffer(device);
    std::vector<SubmissionContext> submission_context_list;

    int num_framesflight = 3;
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

    std::uint32_t frame_index = 0;
    auto last_frame_time = std::chrono::high_resolution_clock::now();
    while(!glfwWindowShouldClose(window_.glfw())) {
      glfwPollEvents();

      if(window_.handle_window_size() == Window::SizeEvent::Changed) {
        swapchain.resize_swapchain(window_.width(), window_.height());
        imgui.create_image(window_.width(), window_.height());
        depth = create_depth_buffer(device);
        render_context_list.clear();
        render_context_list.resize(swapchain.image_count());
        for(std::size_t i = 0; i < render_context_list.size(); ++i) {
          auto&& rc = render_context_list[i];
          rc.target(swapchain.target(i));
          rc.scissor(0, window_.width(), 0, window_.height());
          rc.viewport(window_.width(), window_.height());
          rc.depth(depth);
        }
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

        ImGui::Checkbox("Debug Lights", &debug_lights_);
        for(int i = 0; i < lights.size(); ++i) {
          std::string light_name = "Light ";
          light_name += std::to_string(i);
          ImGui::PushID(light_name.c_str());
          if(ImGui::CollapsingHeader(light_name.c_str())) {
            auto&& light = lights[i];
            light.update_debug_ui();
            ShaderPointLight shader_light;
            shader_light.position = light.position();
            shader_light.colour = light.colour();
            light_data.write(
                &shader_light,
                sizeof(ShaderPointLight) * i,
                sizeof(ShaderPointLight));

            glm::mat4 world_light = glm::translate(glm::mat4(1), shader_light.position);
            light_positions[i].write(&world_light, sizeof(world_light));
          }
          ImGui::PopID();
        }
        ImGui::End();
      }

      auto current = std::chrono::high_resolution_clock::now();
      float dT = std::chrono::duration<float>(current - last_frame_time).count();
      dT = std::clamp(dT, 0.0001f, 0.05f);
      last_frame_time = current;

      update_render(dT);
      view_data.write(&main_camera_, sizeof(main_camera_));
      object_data.write(&main_object_, sizeof(main_object_));

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
      // copy_image.draw(submission_context, background);
      forward_render.draw(submission_context, main_model, view_data, object_data, light_data);

      if(debug_lights_) {
        for(auto&& light : light_positions) {
          debug_draw.draw(submission_context, debug_sphere, view_data, light);
        }
      }

      copy_image_inv_alpha.draw(submission_context, imgui.target().image());
      render_context.finish_rendering(submission_context);
      submission_context.finish_rendering();
      swapchain.present(submission_context);
    }

    swapchain.wait_for_last_frame();
    return false;
  }

 private:
  struct ViewShaderData {
    glm::mat4 projection;
    glm::mat4 view;
  };

  struct ObjectShaderData {
    glm::mat4 model{1};
  };

  void update_render(float dT) {
    update_main_camera(dT);
  }

  struct Scene {
    static constexpr glm::vec3 Up{0.0f, 1.0f, 0.0f};
    static constexpr glm::vec3 Right{1.0f, 0.0f, 0.0f};
    static constexpr glm::vec3 Out{0.0f, 0.0f, 1.0f};
  };

  void update_main_camera(float dT) {
    if(ImGui::Begin("Main View")) {
      ImGui::SliderFloat("Camera Distance", &main_camera_distance_, 0.1f, 50.f);
      ImGui::SliderAngle(
          "Speed",
          &rotation_speed_,
          -360.f,
          360.f,
          "%1.f",
          ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
      ImGui::SameLine();
      ImGui::Checkbox("Rotate Camera", &enable_rotation_);
      ImGui::End();
    }

    if(enable_rotation_) {
      main_object_.model =
          glm::rotate(main_object_.model, dT * rotation_speed_, Scene::Up);
    }

    auto& io = ImGui::GetIO();
    if(!io.WantCaptureMouse) {
      if(io.MouseDown[ImGuiMouseButton_Left]) {
        if(io.MouseDelta.x || io.MouseDelta.y) {
          auto nudge_x = glm::cross(look_, Scene::Up);
          auto nudge_y = glm::cross(nudge_x, look_);
          float rotation_scale = 2.f;
          nudge_x *= (io.MouseDelta.x / (window_.height() * 0.5f)) * rotation_scale;
          nudge_y *= (io.MouseDelta.y / (window_.width() * 0.5f)) * rotation_scale;
          look_ += nudge_x;
          look_ += nudge_y;
          look_ = glm::normalize(look_);
        }
      }
    }

    main_camera_.view = glm::lookAt(
        look_ * main_camera_distance_,
        glm::vec3(0.0f, 0.0f, 0.0f),
        Scene::Up);

    main_camera_.projection = glm::perspective(
        glm::radians(45.0f),
        // Should actually come from the swapchain.
        window_.width() / (window_.height() * 1.f),
        0.1f,
        1000.0f);
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
    check_hr(device->CreateCommittedResource(
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
  glm::vec4 clear_colour_ = {0.0f, 0.0f, 0.0f, 1.f};
  std::vector<char> adapter_names_;
  Window& window_;
  Swapchain* swapchain_ = nullptr;
  Device* device_ = nullptr;
  DepthImage* depth_ = nullptr;
  ViewShaderData main_camera_;
  ObjectShaderData main_object_;
  float main_camera_distance_ = 20.f;
  glm::vec3 look_{0, 0, -1};
  float rotation_speed_ = glm::radians(90.f);
  bool enable_rotation_ = true;
  bool debug_lights_ = false;
};

static void glfw_error_callback(int error, const char* description) {
  std::cerr << "Glfw Error " << error << ": " << description;
}

int main(int, char**) {
  struct DxReport {
    ~DxReport() {
#if RNDRX_ENABLE_DX12_DEBUG_LAYER
      CComPtr<IDXGIDebug1> xgi_debug;
      if(SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&xgi_debug)))) {
        xgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
      }
#endif
    }
  } report_on_exit;

  glfwSetErrorCallback(glfw_error_callback);
  if(!glfwInit()) {
    return 1;
  }

  struct CleanupGlfw {
    ~CleanupGlfw() {
      glfwTerminate();
    }
  } cleanup_glfw;

  Window window;
  Application app(window);

  try {
    while(app.run())
      ;
  }
  catch(std::exception& e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
