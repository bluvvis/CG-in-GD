#include "dx12_renderer.h"

#include "utils/com_error_handler.h"
#include "utils/window.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <chrono>
#include <cstring>
#include <filesystem>


void cg::renderer::dx12_renderer::init()
{
	load_model();
	load_camera();
	load_pipeline();
	load_assets();
}

void cg::renderer::dx12_renderer::destroy()
{
	wait_for_gpu();
	CloseHandle(fence_event);
}

void cg::renderer::dx12_renderer::update()
{
	auto now = std::chrono::high_resolution_clock::now();
	frame_duration = std::chrono::duration<float, std::chrono::seconds::period>(now - current_time).count();
	current_time = now;
	auto world_matrix = DirectX::XMMatrixIdentity();
	auto view_matrix = camera->get_dxm_view_matrix();
	auto projection_matrix = camera->get_dxm_projection_matrix();
	cb.mwpMatrix = world_matrix * view_matrix * projection_matrix;
	memcpy(constant_buffer_data_begin, &cb, sizeof(cb));
}

void cg::renderer::dx12_renderer::render()
{
	populate_command_list();
	ID3D12CommandList* pp_command_lists[] = {command_list.Get()};
	command_queue->ExecuteCommandLists(_countof(pp_command_lists), pp_command_lists);
	THROW_IF_FAILED(swap_chain->Present(1, 0));
	move_to_next_frame();
}

ComPtr<IDXGIFactory4> cg::renderer::dx12_renderer::get_dxgi_factory()
{
	UINT dxgi_factory_flags = 0;
#ifdef _DEBUG
	ComPtr<ID3D12Debug> debug_controller;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
	{
		debug_controller->EnableDebugLayer();
		dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
	}
#endif
	ComPtr<IDXGIFactory4> factory;
	THROW_IF_FAILED(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory)));
	return factory;
}

void cg::renderer::dx12_renderer::initialize_device(ComPtr<IDXGIFactory4>& dxgi_factory)
{
	ComPtr<IDXGIAdapter1> hardware_adapter;
	for (UINT adapter_index = 0; DXGI_ERROR_NOT_FOUND != dxgi_factory->EnumAdapters1(adapter_index, &hardware_adapter); ++adapter_index)
	{
		DXGI_ADAPTER_DESC1 desc;
		hardware_adapter->GetDesc1(&desc);
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			continue;
		}
		if (SUCCEEDED(D3D12CreateDevice(hardware_adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}
	THROW_IF_FAILED(D3D12CreateDevice(hardware_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
}

void cg::renderer::dx12_renderer::create_direct_command_queue()
{
	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	THROW_IF_FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue)));
}

void cg::renderer::dx12_renderer::create_swap_chain(ComPtr<IDXGIFactory4>& dxgi_factory)
{
	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
	swap_chain_desc.BufferCount = frame_number;
	swap_chain_desc.Width = settings->width;
	swap_chain_desc.Height = settings->height;
	swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swap_chain_desc.SampleDesc.Count = 1;
	ComPtr<IDXGISwapChain1> swap_chain1;
	THROW_IF_FAILED(dxgi_factory->CreateSwapChainForHwnd(
		command_queue.Get(),
		cg::utils::window::get_hwnd(),
		&swap_chain_desc,
		nullptr,
		nullptr,
		&swap_chain1));
	THROW_IF_FAILED(swap_chain1.As(&swap_chain));
	frame_index = swap_chain->GetCurrentBackBufferIndex();
}

void cg::renderer::dx12_renderer::create_render_target_views()
{
	rtv_heap.create_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, frame_number);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap.get_cpu_descriptor_handle(0));
	UINT rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (UINT n = 0; n < frame_number; n++)
	{
		THROW_IF_FAILED(swap_chain->GetBuffer(n, IID_PPV_ARGS(&render_targets[n])));
		device->CreateRenderTargetView(render_targets[n].Get(), nullptr, rtv_handle);
		rtv_handle.Offset(1, rtv_descriptor_size);
	}
}

void cg::renderer::dx12_renderer::create_depth_buffer()
{
}

void cg::renderer::dx12_renderer::create_command_allocators()
{
	for (UINT n = 0; n < frame_number; n++)
	{
		THROW_IF_FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocators[n])));
	}
}

void cg::renderer::dx12_renderer::create_command_list()
{
	THROW_IF_FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocators[0].Get(), pipeline_state.Get(), IID_PPV_ARGS(&command_list)));
	command_list->Close();
}


void cg::renderer::dx12_renderer::load_pipeline()
{
	auto dxgi_factory = get_dxgi_factory();
	initialize_device(dxgi_factory);
	create_direct_command_queue();
	create_swap_chain(dxgi_factory);
	create_render_target_views();
}

D3D12_STATIC_SAMPLER_DESC cg::renderer::dx12_renderer::get_sampler_descriptor()
{
	D3D12_STATIC_SAMPLER_DESC sampler_desc = {};
	sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler_desc.MipLODBias = 0;
	sampler_desc.MaxAnisotropy = 0;
	sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler_desc.MinLOD = 0.0f;
	sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
	sampler_desc.ShaderRegister = 0;
	sampler_desc.RegisterSpace = 0;
	sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	return sampler_desc;
}

void cg::renderer::dx12_renderer::create_root_signature(const D3D12_STATIC_SAMPLER_DESC* sampler_descriptors, UINT num_sampler_descriptors)
{
	CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	CD3DX12_ROOT_PARAMETER1 root_parameters[1];
	root_parameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
	root_signature_desc.Init_1_1(_countof(root_parameters), root_parameters, num_sampler_descriptors, sampler_descriptors, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	THROW_IF_FAILED(D3DX12SerializeVersionedRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
	THROW_IF_FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
}

std::filesystem::path cg::renderer::dx12_renderer::get_shader_path()
{
	std::filesystem::path shader_path = settings->shader_path;
	if (shader_path.empty())
	{
		shader_path = std::filesystem::current_path();
		shader_path /= "shaders";
		shader_path /= "shaders.hlsl";
	}
	return shader_path;
}

ComPtr<ID3DBlob> cg::renderer::dx12_renderer::compile_shader(const std::string& entrypoint, const std::string& target)
{
	UINT compile_flags = 0;
#ifdef _DEBUG
	compile_flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	ComPtr<ID3DBlob> error;
	auto shader_path = get_shader_path();
	ComPtr<ID3DBlob> shader;
	HRESULT hr = D3DCompileFromFile(
		shader_path.wstring().c_str(),
		nullptr,
		nullptr,
		entrypoint.c_str(),
		target.c_str(),
		compile_flags,
		0,
		&shader,
		&error);
	if (FAILED(hr))
	{
		if (error)
		{
			std::string error_message = static_cast<const char*>(error->GetBufferPointer());
			THROW_ERROR("Shader compilation failed: " + error_message);
		}
		THROW_IF_FAILED(hr);
	}
	return shader;
}

void cg::renderer::dx12_renderer::create_pso()
{
	auto vertex_shader = compile_shader("VSMain", "vs_5_1");
	auto pixel_shader = compile_shader("PSMain", "ps_5_1");
	D3D12_INPUT_ELEMENT_DESC input_element_descs[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR0", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR1", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR2", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
	pso_desc.InputLayout = {input_element_descs, _countof(input_element_descs)};
	pso_desc.pRootSignature = root_signature.Get();
	pso_desc.VS = CD3DX12_SHADER_BYTECODE(vertex_shader.Get());
	pso_desc.PS = CD3DX12_SHADER_BYTECODE(pixel_shader.Get());
	pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso_desc.DepthStencilState.DepthEnable = FALSE;
	pso_desc.DepthStencilState.StencilEnable = FALSE;
	pso_desc.SampleMask = UINT_MAX;
	pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso_desc.NumRenderTargets = 1;
	pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pso_desc.SampleDesc.Count = 1;
	THROW_IF_FAILED(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state)));
}

void cg::renderer::dx12_renderer::create_resource_on_upload_heap(ComPtr<ID3D12Resource>& resource, UINT size, const std::wstring& name)
{
	auto upload_heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
	THROW_IF_FAILED(device->CreateCommittedResource(
		&upload_heap_properties,
		D3D12_HEAP_FLAG_NONE,
		&buffer_desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource)));
	if (!name.empty())
	{
		resource->SetName(name.c_str());
	}
}

void cg::renderer::dx12_renderer::create_resource_on_default_heap(ComPtr<ID3D12Resource>& resource, UINT size, const std::wstring& name, D3D12_RESOURCE_DESC* resource_descriptor)
{
}

void cg::renderer::dx12_renderer::copy_data(const void* buffer_data, UINT buffer_size, ComPtr<ID3D12Resource>& destination_resource)
{
	UINT8* p_data_begin;
	CD3DX12_RANGE read_range(0, 0);
	THROW_IF_FAILED(destination_resource->Map(0, &read_range, reinterpret_cast<void**>(&p_data_begin)));
	memcpy(p_data_begin, buffer_data, buffer_size);
	destination_resource->Unmap(0, nullptr);
}

void cg::renderer::dx12_renderer::copy_data(const void* buffer_data, const UINT buffer_size, ComPtr<ID3D12Resource>& destination_resource, ComPtr<ID3D12Resource>& intermediate_resource, D3D12_RESOURCE_STATES state_after, int row_pitch, int slice_pitch)
{
}

D3D12_VERTEX_BUFFER_VIEW cg::renderer::dx12_renderer::create_vertex_buffer_view(const ComPtr<ID3D12Resource>& vertex_buffer, const UINT vertex_buffer_size)
{
	D3D12_VERTEX_BUFFER_VIEW view;
	view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
	view.SizeInBytes = vertex_buffer_size;
	view.StrideInBytes = sizeof(cg::vertex);
	return view;
}

D3D12_INDEX_BUFFER_VIEW cg::renderer::dx12_renderer::create_index_buffer_view(const ComPtr<ID3D12Resource>& index_buffer, const UINT index_buffer_size)
{
	D3D12_INDEX_BUFFER_VIEW view;
	view.BufferLocation = index_buffer->GetGPUVirtualAddress();
	view.SizeInBytes = index_buffer_size;
	view.Format = DXGI_FORMAT_R32_UINT;
	return view;
}

void cg::renderer::dx12_renderer::create_shader_resource_view(const ComPtr<ID3D12Resource>& texture, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handler)
{
}

void cg::renderer::dx12_renderer::create_constant_buffer_view(const ComPtr<ID3D12Resource>& buffer, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handler)
{
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
	cbv_desc.BufferLocation = buffer->GetGPUVirtualAddress();
	cbv_desc.SizeInBytes = (sizeof(constant_buffer) + 255) & ~255;
	device->CreateConstantBufferView(&cbv_desc, cpu_handler);
}

void cg::renderer::dx12_renderer::load_assets()
{
	auto sampler_descriptor = get_sampler_descriptor();
	create_root_signature(&sampler_descriptor, 1);
	create_pso();
	create_command_allocators();
	create_command_list();
	cbv_srv_heap.create_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
	view_port = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(settings->width), static_cast<float>(settings->height));
	scissor_rect = CD3DX12_RECT(0, 0, static_cast<LONG>(settings->width), static_cast<LONG>(settings->height));
	vertex_buffers.resize(model->get_vertex_buffers().size());
	upload_vertex_buffers.resize(model->get_vertex_buffers().size());
	vertex_buffer_views.resize(model->get_vertex_buffers().size());
	index_buffers.resize(model->get_index_buffers().size());
	upload_index_buffers.resize(model->get_index_buffers().size());
	index_buffer_views.resize(model->get_index_buffers().size());
	for (size_t s = 0; s < model->get_vertex_buffers().size(); s++)
	{
		auto vertex_buffer_size = static_cast<UINT>(model->get_vertex_buffers()[s]->size_bytes());
		create_resource_on_upload_heap(upload_vertex_buffers[s], vertex_buffer_size);
		copy_data(model->get_vertex_buffers()[s]->get_data(), vertex_buffer_size, upload_vertex_buffers[s]);
		vertex_buffers[s] = upload_vertex_buffers[s];
		vertex_buffer_views[s] = create_vertex_buffer_view(vertex_buffers[s], vertex_buffer_size);
	}
	for (size_t s = 0; s < model->get_index_buffers().size(); s++)
	{
		auto index_buffer_size = static_cast<UINT>(model->get_index_buffers()[s]->size_bytes());
		create_resource_on_upload_heap(upload_index_buffers[s], index_buffer_size);
		copy_data(model->get_index_buffers()[s]->get_data(), index_buffer_size, upload_index_buffers[s]);
		index_buffers[s] = upload_index_buffers[s];
		index_buffer_views[s] = create_index_buffer_view(index_buffers[s], index_buffer_size);
	}
	const UINT constant_buffer_size = (sizeof(constant_buffer) + 255) & ~255;
	create_resource_on_upload_heap(constant_buffer, constant_buffer_size);
	CD3DX12_RANGE read_range(0, 0);
	THROW_IF_FAILED(constant_buffer->Map(0, &read_range, reinterpret_cast<void**>(&constant_buffer_data_begin)));
	create_constant_buffer_view(constant_buffer, cbv_srv_heap.get_cpu_descriptor_handle(0));
	for (UINT n = 0; n < frame_number; n++)
	{
		fence_values[n] = 0;
	}
	THROW_IF_FAILED(device->CreateFence(fence_values[frame_index], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	fence_values[frame_index]++;
	fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (fence_event == nullptr)
	{
		THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
	}
}


void cg::renderer::dx12_renderer::populate_command_list()
{
	THROW_IF_FAILED(command_allocators[frame_index]->Reset());
	THROW_IF_FAILED(command_list->Reset(command_allocators[frame_index].Get(), pipeline_state.Get()));
	command_list->SetGraphicsRootSignature(root_signature.Get());
	command_list->RSSetViewports(1, &view_port);
	command_list->RSSetScissorRects(1, &scissor_rect);
	command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(render_targets[frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap.get_cpu_descriptor_handle(frame_index));
	command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
	const float clear_color[] = {0.0f, 0.2f, 0.4f, 1.0f};
	command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
	command_list->SetGraphicsRootDescriptorTable(0, cbv_srv_heap.get_gpu_descriptor_handle(0));
	for (size_t s = 0; s < model->get_vertex_buffers().size(); s++)
	{
		command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		command_list->IASetVertexBuffers(0, 1, &vertex_buffer_views[s]);
		command_list->IASetIndexBuffer(&index_buffer_views[s]);
		command_list->DrawIndexedInstanced(static_cast<UINT>(model->get_index_buffers()[s]->count()), 1, 0, 0, 0);
	}
	command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(render_targets[frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
	THROW_IF_FAILED(command_list->Close());
}


void cg::renderer::dx12_renderer::move_to_next_frame()
{
	const UINT64 current_fence_value = fence_values[frame_index];
	THROW_IF_FAILED(command_queue->Signal(fence.Get(), current_fence_value));
	frame_index = swap_chain->GetCurrentBackBufferIndex();
	if (fence->GetCompletedValue() < fence_values[frame_index])
	{
		THROW_IF_FAILED(fence->SetEventOnCompletion(fence_values[frame_index], fence_event));
		WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
	}
	fence_values[frame_index] = current_fence_value + 1;
}

void cg::renderer::dx12_renderer::wait_for_gpu()
{
	THROW_IF_FAILED(command_queue->Signal(fence.Get(), fence_values[frame_index]));
	THROW_IF_FAILED(fence->SetEventOnCompletion(fence_values[frame_index], fence_event));
	WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
	fence_values[frame_index]++;
}


void cg::renderer::descriptor_heap::create_heap(ComPtr<ID3D12Device>& device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT number, D3D12_DESCRIPTOR_HEAP_FLAGS flags)
{
	D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
	heap_desc.NumDescriptors = number;
	heap_desc.Type = type;
	heap_desc.Flags = flags;
	THROW_IF_FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)));
	descriptor_size = device->GetDescriptorHandleIncrementSize(type);
}

D3D12_CPU_DESCRIPTOR_HANDLE cg::renderer::descriptor_heap::get_cpu_descriptor_handle(UINT index) const
{
	auto handle = heap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += index * descriptor_size;
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE cg::renderer::descriptor_heap::get_gpu_descriptor_handle(UINT index) const
{
	auto handle = heap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += index * descriptor_size;
	return handle;
}
ID3D12DescriptorHeap* cg::renderer::descriptor_heap::get() const
{
	return heap.Get();
}
